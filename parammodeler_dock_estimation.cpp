#include "parammodeler_dock.h"
#include "ui_parammodeler_dock.h"
#include "buildmesh.h"
#include "parammodeler_dlutils.h"
#include "parammodeler_pcdloader.h"
#include "parammodeler_config.h"
#include "parammodeler_pointnet.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <windows.h>

#define DEBUG_LOG( msg ) OutputDebugStringW( msg )

void ParamModelerDock::onOpenPointCloudEstimateDialog()
{
  // Inference pumps Qt events; keep the modal workflow alive until it returns.
  class EstimateDialog : public QDialog
  {
  public:
    using QDialog::QDialog;
    bool busy = false;
    void reject() override { if ( !busy ) QDialog::reject(); }
  protected:
    void closeEvent( QCloseEvent *event ) override
    {
      if ( busy ) event->ignore();
      else QDialog::closeEvent( event );
    }
  };
  EstimateDialog dialog( this );
  dialog.setWindowTitle( tr( "Point cloud classification and parameter estimation" ) );
  dialog.resize( 560, 520 );

  auto *mainLayout = new QVBoxLayout( &dialog );
  auto *inputTitle = new QLabel( tr( "1. Input point cloud" ), &dialog );
  inputTitle->setStyleSheet( QStringLiteral( "font-weight: bold;" ) );
  auto *inputInfo = new QLabel( &dialog );
  inputInfo->setWordWrap( true );
  inputInfo->setStyleSheet( QStringLiteral( "color: #555;" ) );
  auto *btnLoad = new QPushButton( tr( "Load point cloud" ), &dialog );

  auto *processTitle = new QLabel( tr( "2. Classification and parameter estimation" ), &dialog );
  processTitle->setStyleSheet( QStringLiteral( "font-weight: bold; margin-top: 8px;" ) );
  auto *modelLayout = new QHBoxLayout();
  auto *modelLabel = new QLabel( tr( "Model:" ), &dialog );
  auto *comboModel = new QComboBox( &dialog );
  comboModel->addItem( QStringLiteral( "PTv3" ) );
  comboModel->addItem( QStringLiteral( "PCT" ) );
  comboModel->setFixedWidth( 160 );
  modelLayout->addWidget( modelLabel );
  modelLayout->addWidget( comboModel );

  auto *btnSettings = new QPushButton( tr( "⚙" ), &dialog );
  btnSettings->setFixedSize( 28, 28 );
  btnSettings->setToolTip( tr( "PointNet path settings" ) );
  btnSettings->setCursor( Qt::PointingHandCursor );
  btnSettings->setFlat( true );
  connect( btnSettings, &QPushButton::clicked, &dialog, [&dialog]() {
    ParamModelerConfig::showSettingsDialog( &dialog );
  } );
  modelLayout->addWidget( btnSettings );

  modelLayout->addStretch();
  auto *chkGeometryCorrection = new QCheckBox( tr( "Enable geometry correction" ), &dialog );
  chkGeometryCorrection->setToolTip(
    tr( "Experimental: after model regression, blend supported parameters with geometry measured from the current point cloud." )
  );
  chkGeometryCorrection->setChecked( m_geometryCorrectionEnabled );
  connect( chkGeometryCorrection, &QCheckBox::toggled, this, [this]( bool checked ) {
    m_geometryCorrectionEnabled = checked;
  } );
  auto *correctionStrengthLayout = new QHBoxLayout();
  auto *correctionStrengthLabel = new QLabel( tr( "Geometry correction strength:" ), &dialog );
  auto *spinGeometryStrength = new QSpinBox( &dialog );
  spinGeometryStrength->setRange( 0, 100 );
  spinGeometryStrength->setSuffix( tr( "%" ) );
  spinGeometryStrength->setValue( qBound( 0, static_cast<int>( std::round( m_geometryCorrectionStrength * 100.0 ) ), 100 ) );
  spinGeometryStrength->setToolTip(
    tr( "0% keeps the model prediction; 100% uses the measured point-cloud geometry. Intermediate values blend both." )
  );
  spinGeometryStrength->setEnabled( m_geometryCorrectionEnabled );
  connect( spinGeometryStrength, QOverload<int>::of( &QSpinBox::valueChanged ), this, [this]( int value ) {
    m_geometryCorrectionStrength = qBound( 0.0, value / 100.0, 1.0 );
  } );
  connect( chkGeometryCorrection, &QCheckBox::toggled, spinGeometryStrength, &QSpinBox::setEnabled );
  correctionStrengthLayout->addWidget( correctionStrengthLabel );
  correctionStrengthLayout->addWidget( spinGeometryStrength );
  correctionStrengthLayout->addStretch();

  auto *resultLabel = new QLabel( tr( "Result: -" ), &dialog );
  resultLabel->setWordWrap( true );
  resultLabel->setStyleSheet( QStringLiteral( "font-weight: bold; padding: 4px 0;" ) );

  auto *progress = new QProgressBar( &dialog );
  progress->setVisible( false );
  progress->setTextVisible( false );

  auto *buttonLayout = new QHBoxLayout();
  auto *btnClassify = new QPushButton( tr( "Classify" ), &dialog );
  auto *btnInverse = new QPushButton( tr( "Estimate parameters" ), &dialog );
  auto *btnComplete = new QPushButton( tr( "Complete all" ), &dialog );
  btnComplete->setObjectName( QStringLiteral( "btnCompleteEstimate" ) );
  auto *btnFinish = new QPushButton( tr( "Return to fine tuning" ), &dialog );
  btnClassify->setMinimumHeight( 28 );
  btnInverse->setMinimumHeight( 28 );
  btnComplete->setMinimumHeight( 28 );
  btnFinish->setMinimumHeight( 28 );
  buttonLayout->addWidget( btnClassify );
  buttonLayout->addWidget( btnInverse );
  buttonLayout->addWidget( btnComplete );
  buttonLayout->addWidget( btnFinish );

  auto *table = new QTableWidget( &dialog );
  table->setColumnCount( 2 );
  table->setHorizontalHeaderLabels( QStringList() << tr( "Parameter" ) << tr( "Value" ) );
  table->horizontalHeader()->setStretchLastSection( true );
  table->verticalHeader()->setVisible( false );
  table->setAlternatingRowColors( true );
  table->setSelectionBehavior( QAbstractItemView::SelectRows );
  table->setEditTriggers( QAbstractItemView::NoEditTriggers );
  table->setMinimumHeight( 160 );
  bool parametersApplied = false;
  bool inputReady = false;
  bool classified = false;
  QString classificationSummary;

  const auto clearEstimate = [&]() {
    parametersApplied = false;
    table->setRowCount( 0 );
    m_evaluationRaw.clear();
    m_evaluationCorrected.clear();
    m_dlAnchorParams.clear();
    m_hasDlAnchor = false;
    if ( m_resetAnchorBtn ) m_resetAnchorBtn->setEnabled( false );
  };
  const auto setBusy = [&]( bool busy ) {
    dialog.busy = busy;
    btnLoad->setEnabled( !busy );
    btnSettings->setEnabled( !busy );
    comboModel->setEnabled( !busy );
    chkGeometryCorrection->setEnabled( !busy );
    spinGeometryStrength->setEnabled( !busy && chkGeometryCorrection->isChecked() );
    btnClassify->setEnabled( !busy && inputReady );
    btnInverse->setEnabled( !busy && inputReady && classified );
    btnComplete->setEnabled( !busy && inputReady );
    btnFinish->setEnabled( !busy );
    progress->setRange( 0, busy ? 0 : 100 );
    progress->setVisible( busy );
  };

  mainLayout->addWidget( inputTitle );
  mainLayout->addWidget( btnLoad );
  mainLayout->addWidget( inputInfo );
  mainLayout->addWidget( processTitle );
  mainLayout->addLayout( modelLayout );
  mainLayout->addWidget( chkGeometryCorrection );
  mainLayout->addLayout( correctionStrengthLayout );
  mainLayout->addWidget( resultLabel );
  mainLayout->addWidget( progress );
  mainLayout->addLayout( buttonLayout );
  mainLayout->addWidget( table );

  auto updateInputInfo = [&]() {
    inputReady = false;
    classified = false;
    parametersApplied = false;
    table->setRowCount( 0 );
    resultLabel->setText( tr( "Result: -" ) );
    setBusy( false );
    if ( m_inputDataPath.isEmpty() )
    {
      inputInfo->setText( tr( "No point cloud loaded" ) );
      btnClassify->setEnabled( false );
      btnInverse->setEnabled( false );
      return;
    }

    const QFileInfo fi( m_inputDataPath );
    PointCloud pc = PointCloudLoader::load( m_inputDataPath );
    if ( pc.points.isEmpty() )
    {
      inputInfo->setText( tr( "Selected: %1\nFailed to read point cloud or point count is 0." ).arg( fi.fileName() ) );
      btnClassify->setEnabled( false );
      btnInverse->setEnabled( false );
      return;
    }

    inputInfo->setText(
      tr( "Loaded: %1\nPoints: %2\nX: [%3, %4]\nY: [%5, %6]\nZ: [%7, %8]" )
        .arg( fi.fileName() )
        .arg( pc.points.size() )
        .arg( pc.bboxMin.x(), 0, 'f', 3 )
        .arg( pc.bboxMax.x(), 0, 'f', 3 )
        .arg( pc.bboxMin.y(), 0, 'f', 3 )
        .arg( pc.bboxMax.y(), 0, 'f', 3 )
        .arg( pc.bboxMin.z(), 0, 'f', 3 )
        .arg( pc.bboxMax.z(), 0, 'f', 3 )
    );
    btnClassify->setEnabled( true );
    inputReady = true;
    btnComplete->setEnabled( true );
    btnInverse->setEnabled( false );
    resultLabel->setText( tr( "Result: -" ) );
    table->setRowCount( 0 );
    parametersApplied = false;
  };

  connect( btnLoad, &QPushButton::clicked, &dialog, [&]() {
    const QString filePath = QFileDialog::getOpenFileName(
      &dialog, tr( "Load point cloud" ), "",
      tr( "Point cloud files (*.ply *.las *.laz *.xyz *.txt)" )
    );
    if ( filePath.isEmpty() )
      return;

    m_inputDataPath = filePath;
    clearEstimate();
    cacheInputMetadata( filePath );   // 对话框流程也要缓存，否则对齐没有退化备选
    updateInputInfo();
  } );

  auto selectedBackend = [&]() {
    const QString modelName = comboModel->currentText();
    if ( modelName == QStringLiteral( "PTv3" ) )
      return PointNetBackend::PTv3;
    return PointNetBackend::PCT;
  };

  const auto classify = [&]() -> bool {
    if ( m_inputDataPath.isEmpty() )
      return false;

    classified = false;
    clearEstimate();
    resultLabel->setText( tr( "Classifying..." ) );
    QApplication::processEvents();

    PointNetPredictResult result = PointNetRunner::predict( m_inputDataPath, selectedBackend(), 2048, 3 );

    if ( !result.errorMessage.isEmpty() )
    {
      QMessageBox::warning( &dialog, tr( "Classification failed" ), result.errorMessage );
      resultLabel->setText( tr( "Result: -" ) );
      return false;
    }
    if ( result.predictions.isEmpty() )
    {
      QMessageBox::warning( &dialog, tr( "Classification failed" ), tr( "No prediction returned." ) );
      resultLabel->setText( tr( "Result: -" ) );
      return false;
    }

    const PointNetPrediction top1 = result.predictions.first();
    if ( ui->comboPrimitive->findText( top1.className ) < 0 )
    {
      QMessageBox::warning( &dialog, tr( "Classification failed" ), tr( "Unsupported primitive: %1" ).arg( top1.className ) );
      resultLabel->setText( tr( "Result: -" ) );
      return false;
    }
    resultLabel->setText(
      tr( "Result: %1\nConfidence: %2%\nSwitched to corresponding primitive. You can estimate parameters and return to fine tuning." )
        .arg( top1.className )
        .arg( top1.probability * 100.0, 0, 'f', 1 )
    );
    ui->comboPrimitive->setCurrentText( top1.className );
    classificationSummary = resultLabel->text();
    classified = true;
    return true;
  };

  const auto estimate = [&]() -> bool {
    if ( m_inputDataPath.isEmpty() )
      return false;

    clearEstimate();
    resultLabel->setText( tr( "Estimating parameters..." ) );
    QApplication::processEvents();

    const QString prim = ui->comboPrimitive->currentText();
    const PointNetRegressionResult regression = PointNetRunner::predictParams( m_inputDataPath, selectedBackend(), prim, 2048 );

    if ( !regression.errorMessage.isEmpty() )
    {
      resultLabel->setText( tr( "Parameter estimation failed" ) );
      QMessageBox::warning( &dialog, tr( "Parameter estimation failed" ), regression.errorMessage );
      return false;
    }

    const bool swapCanonicalAxes = shouldSwapCanonicalRegressionAxes(
      prim, regression.params, m_inputDataPath, m_hasMetadataRz ? m_metadataRz : qQNaN() );
    QMap<QString, double> params = pointNetParamsToUiParams( prim, regression.params, swapCanonicalAxes );
    const auto rawParams = params;
    applyDataDrivenParamCorrections( prim, params );
    if ( params.isEmpty() )
    {
      resultLabel->setText( tr( "Parameter estimation failed" ) );
      QMessageBox::warning( &dialog, tr( "Parameter estimation failed" ), tr( "No parameters returned." ) );
      return false;
    }

    PointNetRunner::applyToUI( this, params );
    m_evaluationRaw = rawParams;
    m_evaluationCorrected = params;
    m_evaluationInput = QFileInfo( m_inputDataPath ).absoluteFilePath();
    m_evaluationPrimitive = prim;
    m_evaluationModel = regression.modelName;
    m_evaluationCheckpoint = regression.checkpointPath;
    m_evaluationCorrectionEnabled = m_geometryCorrectionEnabled;
    m_evaluationCorrectionStrength = m_geometryCorrectionEnabled ? m_geometryCorrectionStrength : 0.0;

    // 保存 DL 预测值作为微调锚点（对话框流程也需要）
    m_dlAnchorParams = params;
    m_hasDlAnchor = true;
    if ( m_resetAnchorBtn )
      m_resetAnchorBtn->setEnabled( true );

    parametersApplied = true;
    table->setRowCount( params.size() );
    int row = 0;
    for ( auto it = params.cbegin(); it != params.cend(); ++it, ++row )
    {
      table->setItem( row, 0, new QTableWidgetItem( it.key() ) );
      table->setItem( row, 1, new QTableWidgetItem( QString::number( it.value(), 'f', 2 ) ) );
    }
    onUpdatePreview();
    resultLabel->setText( classificationSummary + tr( "\nParameters applied to main panel." ) );
    return true;
  };

  const auto loadAndAlign = [&]() -> bool {
    resultLabel->setText( tr( "Loading and aligning..." ) );
    if ( m_isUpdating || BuildMesh::build( ui->comboPrimitive->currentText(), this ).isEmpty() )
    {
      QMessageBox::warning( &dialog, tr( "Load failed" ), tr( "Current parameters cannot generate a valid model." ) );
      return false;
    }
    onLoadToQGIS3D( false );
    if ( !m_realtimeModelLoaded )
    {
      QMessageBox::warning( &dialog, tr( "Load failed" ), tr( "Could not load the model into QGIS 3D." ) );
      return false;
    }
    if ( !loadPointCloudToQGIS3D( m_inputDataPath, false ) )
    {
      QMessageBox::warning( &dialog, tr( "Point cloud load failed" ), tr( "The model was loaded, but the point cloud was not loaded into QGIS 3D." ) );
      return false;
    }
    if ( !applyMetadataRz() )
      DEBUG_LOG( QString( "[Align] no metadata rz for %1, keeping current pose rotation\n" ).arg( m_inputDataPath ).toStdWString().c_str() );
    QVector3D alignTarget;
    bool semanticUpper = false;
    if ( !pointCloudAlignTarget( alignTarget, &semanticUpper ) )
    {
      QMessageBox::warning( &dialog, tr( "Alignment unavailable" ), tr( "No point-cloud alignment reference is available. Use manual alignment." ) );
      return false;
    }
    alignModelToPointCloud( BuildMesh::build( ui->comboPrimitive->currentText(), this ),
                            alignTarget, ui->comboPrimitive->currentText(),
                            QStringLiteral( "dialog" ), semanticUpper );
    onUpdatePreview();
    return true;
  };
  const auto run = [&]( const auto &operation ) -> bool {
    if ( dialog.busy || !inputReady ) return false;
    setBusy( true );
    const bool ok = operation();
    setBusy( false );
    if ( !ok && parametersApplied )
      resultLabel->setText( tr( "Loading or alignment failed. You can retry or return to fine tuning." ) );
    return ok;
  };
  connect( btnClassify, &QPushButton::clicked, &dialog, [&] { run( classify ); } );
  connect( btnInverse, &QPushButton::clicked, &dialog, [&] { run( estimate ); } );
  connect( btnComplete, &QPushButton::clicked, &dialog, [&] {
    if ( run( [&] { return classify() && estimate() && loadAndAlign(); } ) ) dialog.accept();
  } );

  connect( btnFinish, &QPushButton::clicked, &dialog, [&]() {
    if ( dialog.busy ) return;
    if ( parametersApplied && !m_inputDataPath.isEmpty() )
    {
      const QMessageBox::StandardButton answer = QMessageBox::question(
        &dialog,
        tr( "Load to QGIS 3D" ),
        tr( "Load the input point cloud and the estimated model into the QGIS 3D scene now?" ),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::Yes
      );
      if ( answer == QMessageBox::Yes && !run( loadAndAlign ) ) return;
    }
    dialog.accept();
  } );

  updateInputInfo();
  dialog.exec();
}

void ParamModelerDock::onLoadInputData()
{
  QString filePath = QFileDialog::getOpenFileName(
    this, tr( "Load point cloud" ), "",
    tr( "Point cloud files (*.ply *.las *.laz *.xyz *.txt)" )
  );
  if ( filePath.isEmpty() )
    return;

  m_inputDataPath = filePath;
  QFileInfo fi( filePath );

  // 缓存元数据：center（质心）用于反归一化，bbox 极值/底面用于模型对齐 —— 两者语义不同，别混用
  cacheInputMetadata( filePath );

  DEBUG_LOG( QString( "[Tab2] load input data: %1\n" ).arg( filePath ).toStdWString().c_str() );

  ui->labelInputInfo->setText( tr( "Loaded: %1" ).arg( fi.fileName() ) );
  ui->labelPrimitiveType->setText( tr( "Result: -" ) );
  ui->tableInverseParams->setRowCount( 0 );


  ui->btnPointNetClassify->setEnabled( true );
  ui->btnInverseParams->setEnabled( false );
}

void ParamModelerDock::onPointNetClassify()
{
  if ( m_inputDataPath.isEmpty() )
    return;

  ui->progressInversion->setRange( 0, 0 );
  ui->progressInversion->setVisible( true );
  ui->labelPrimitiveType->setText( tr( "Classifying..." ) );
  ui->btnPointNetClassify->setEnabled( false );
  ui->btnInverseParams->setEnabled( false );
  QApplication::processEvents();

  DEBUG_LOG( QString( "[PointNet] predict input: %1\n" ).arg( m_inputDataPath ).toStdWString().c_str() );
  PointNetPredictResult result = PointNetRunner::predict( m_inputDataPath, 2048, 3 );
  ui->progressInversion->setRange( 0, 100 );
  ui->progressInversion->setValue( 100 );
  ui->progressInversion->setVisible( false );
  ui->btnPointNetClassify->setEnabled( true );

  if ( !result.errorMessage.isEmpty() )
  {
    QMessageBox::warning( this, tr( "Classification failed" ), result.errorMessage );
    return;
  }
  if ( result.predictions.isEmpty() )
  {
    QMessageBox::warning( this, tr( "Classification failed" ), tr( "No prediction returned." ) );
    return;
  }

  const PointNetPrediction top1 = result.predictions.first();
  ui->labelPrimitiveType->setText(
    tr( "Result: %1 (%2%)" )
      .arg( top1.className )
      .arg( top1.probability * 100.0, 0, 'f', 1 )
  );

  ui->comboPrimitive->setCurrentText( top1.className );
  ui->btnInverseParams->setEnabled( true );

  DEBUG_LOG( QString( "[PointNet] top1: %1, prob: %2\n" )
               .arg( top1.className )
               .arg( top1.probability )
               .toStdWString()
               .c_str() );
}
// ========================================================================

// ========================================================================
void ParamModelerDock::onInverseParams()
{
  if ( m_inputDataPath.isEmpty() )
    return;

  ui->progressInversion->setVisible( true );
  ui->progressInversion->setValue( 0 );

  QString prim = ui->comboPrimitive->currentText();

  DEBUG_LOG( QString( "[PointNet] parameter regression input: primitive: %1, file: %2\n" ).arg( prim ).arg( m_inputDataPath ).toStdWString().c_str() );
  const PointNetRegressionResult regression = PointNetRunner::predictParams( m_inputDataPath, prim, 2048 );
  if ( !regression.errorMessage.isEmpty() )
  {
    ui->progressInversion->setVisible( false );
    QMessageBox::warning( this, tr( "Parameter estimation failed" ), regression.errorMessage );
    return;
  }
  const bool swapCanonicalAxes = shouldSwapCanonicalRegressionAxes(
    prim, regression.params, m_inputDataPath, m_hasMetadataRz ? m_metadataRz : qQNaN() );
  QMap<QString, double> params = pointNetParamsToUiParams( prim, regression.params, swapCanonicalAxes );
  const auto rawParams = params;
  applyDataDrivenParamCorrections( prim, params );

  if ( !params.isEmpty() )
  {
    DEBUG_LOG( QString( "[PointNet] parameter regression done, returned %1 parameters\n" ).arg( params.size() ).toStdWString().c_str() );
    PointNetRunner::applyToUI( this, params );
    m_evaluationRaw = rawParams;
    m_evaluationCorrected = params;
    m_evaluationInput = QFileInfo( m_inputDataPath ).absoluteFilePath();
    m_evaluationPrimitive = prim;
    m_evaluationModel = regression.modelName;
    m_evaluationCheckpoint = regression.checkpointPath;
    m_evaluationCorrectionEnabled = m_geometryCorrectionEnabled;
    m_evaluationCorrectionStrength = m_geometryCorrectionEnabled ? m_geometryCorrectionStrength : 0.0;

    // 保存 DL 预测值作为微调锚点，供 resetToDlAnchor() 一键复位
    m_dlAnchorParams = params;
    m_hasDlAnchor = true;
    if ( m_resetAnchorBtn )
      m_resetAnchorBtn->setEnabled( true );

    ui->tableInverseParams->setRowCount( params.size() );
    int row = 0;
    for ( auto it = params.cbegin(); it != params.cend(); ++it, ++row )
    {
      ui->tableInverseParams->setItem( row, 0, new QTableWidgetItem( it.key() ) );
      ui->tableInverseParams->setItem( row, 1, new QTableWidgetItem( QString::number( it.value(), 'f', 2 ) ) );
    }
  }

  ui->progressInversion->setValue( 100 );
  ui->progressInversion->setVisible( false );

  // --- 朝向：DL 不预测 rz（标签不可辨识，见 applyMetadataRz），回填 metadata 的导出真值 ---
  // applyToUI 已在上面执行 → metadata 的 rz 覆盖模型可能给出的值；
  // rz 同时记进 DL 锚点并补一行表格，这样"一键复位"恢复的是完整的自动估计结果（含朝向）。
  if ( applyMetadataRz() )
  {
    if ( m_hasDlAnchor )
    {
      m_dlAnchorParams.insert( QStringLiteral( "poseRotateZ" ), m_metadataRz );
      const int row = ui->tableInverseParams->rowCount();
      ui->tableInverseParams->setRowCount( row + 1 );
      ui->tableInverseParams->setItem( row, 0, new QTableWidgetItem( QStringLiteral( "poseRotateZ (metadata)" ) ) );
      ui->tableInverseParams->setItem( row, 1, new QTableWidgetItem( QString::number( m_metadataRz, 'f', 2 ) ) );
    }
  }
  else
  {
    DEBUG_LOG( QString( "[Align] no metadata rz for %1, keeping current pose rotation\n" ).arg( m_inputDataPath ).toStdWString().c_str() );
  }

  // --- Auto-align：首批角锚定类使用左下墙顶/檐口角，其余类保留 bbox/中心参考点 ---
  QVector3D alignTarget;
  bool semanticUpper = false;
  if ( pointCloudAlignTarget( alignTarget, &semanticUpper ) )
    alignModelToPointCloud( BuildMesh::build( ui->comboPrimitive->currentText(), this ),
                            alignTarget, ui->comboPrimitive->currentText(),
                            QStringLiteral( "onInverseParams" ), semanticUpper );
  else
    DEBUG_LOG( QString( "[Align] no point cloud bbox for %1, skipping\n" ).arg( m_inputDataPath ).toStdWString().c_str() );

  onUpdatePreview();
}

// ============================================================
// 一键复位：将所有参数恢复到 DL 推理的预测值
// ============================================================
void ParamModelerDock::resetToDlAnchor()
{
  if ( !m_hasDlAnchor || m_dlAnchorParams.isEmpty() )
    return;

  PointNetRunner::applyToUI( this, m_dlAnchorParams );
  schedulePreviewUpdate();

  DEBUG_LOG( L"[DL Anchor] parameters reset to DL prediction\n" );
}
