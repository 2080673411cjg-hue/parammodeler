/***************************************************************************
  parammodeler_config.cpp
  User-configurable paths for PointNet deep learning backend
  -------------------
         begin                : July 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

#include "parammodeler_config.h"
#include <QDialog>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QgsSettings.h>

// ------------------------------------------------------------------
// helpers
// ------------------------------------------------------------------

static QString setting( const QString &key, const QString &defaultVal )
{
  const QString v = QgsSettings().value( key ).toString();
  return v.isEmpty() ? defaultVal : v;
}

static QString ensureTrailingSlash( const QString &path )
{
  if ( path.endsWith( QLatin1Char( '/' ) ) || path.endsWith( QLatin1Char( '\\' ) ) )
    return path;
  return path + QLatin1Char( '/' );
}

// ------------------------------------------------------------------
// base paths
// ------------------------------------------------------------------

namespace ParamModelerConfig
{

QString pythonExe()
{
  return setting( QStringLiteral( "parammodeler/pythonExe" ),
                  QStringLiteral( "E:/mambaforge/envs/pointnet_train/python.exe" ) );
}

QString pointnetBaseDir()
{
  return ensureTrailingSlash( setting( QStringLiteral( "parammodeler/pointnetBase" ),
                                       QStringLiteral( "E:/pointnet" ) ) );
}

QString datasetsBaseDir()
{
  return ensureTrailingSlash( setting( QStringLiteral( "parammodeler/datasetsBase" ),
                                       QStringLiteral( "E:/pointnet/datasets_aug" ) ) );
}

// ------------------------------------------------------------------
// model version selectors
// ------------------------------------------------------------------

QString classifyModelName()
{
  return setting( QStringLiteral( "parammodeler/classifyModelName" ),
                  QStringLiteral( "pct_cls_v2" ) );
}

QString regressionModelPrefix()
{
  return setting( QStringLiteral( "parammodeler/regressionModelPrefix" ),
                  QStringLiteral( "pointnext_reg_" ) );
}

QString regressionModelSuffix()
{
  return setting( QStringLiteral( "parammodeler/regressionModelSuffix" ),
                  QStringLiteral( "_aux" ) );
}

// ------------------------------------------------------------------
// PCT model variant (separate from PointNeXt legacy settings)
// ------------------------------------------------------------------

QString pctRegressionSuffix()
{
  return setting( QStringLiteral( "parammodeler/pctRegressionSuffix" ),
                  QStringLiteral( "_v2_neighbor" ) );
}

// ------------------------------------------------------------------
// derived classify paths
// ------------------------------------------------------------------

QString classifyScript( PointNetBackend backend )
{
  const QString base = pointnetBaseDir();
  switch ( backend )
  {
    case PointNetBackend::PointNet:  return base + QStringLiteral( "pointnet_simple/main.py" );
    case PointNetBackend::PointNeXt: return base + QStringLiteral( "pointnext_simple/main.py" );
    case PointNetBackend::PCT:       return base + QStringLiteral( "pct_simple/main.py" );
    default:                         return base + QStringLiteral( "pointnet2_simple/main.py" );
  }
}

QString classifyLogDir( PointNetBackend backend )
{
  const QString base = pointnetBaseDir();
  switch ( backend )
  {
    case PointNetBackend::PointNet:  return base + QStringLiteral( "pointnet_simple/logs/pointnet_aug_roof_guard_v1" );
    case PointNetBackend::PointNeXt: return base + QStringLiteral( "pointnext_simple/logs/" ) + classifyModelName();
    case PointNetBackend::PCT:       return base + QStringLiteral( "pct_simple/logs/pct_cls_v2" );
    default:                         return base + QStringLiteral( "pointnet2_simple/logs/pointnet2_cls_auxdata_250" );
  }
}

// ------------------------------------------------------------------
// derived regression paths
// ------------------------------------------------------------------

QString regressionScript( PointNetBackend backend )
{
  const QString base = pointnetBaseDir();
  if ( backend == PointNetBackend::PointNeXt )
    return base + QStringLiteral( "pointnext_simple/main_reg.py" );
  if ( backend == PointNetBackend::PCT )
    return base + QStringLiteral( "pct_simple/main_reg.py" );
  return base + QStringLiteral( "pointnet2_simple/main_reg.py" );
}

QString regressionLogBase( PointNetBackend backend )
{
  const QString base = pointnetBaseDir();
  if ( backend == PointNetBackend::PointNeXt )
    return base + QStringLiteral( "pointnext_simple/logs/" );
  if ( backend == PointNetBackend::PCT )
    return base + QStringLiteral( "pct_simple/logs/" );
  return base + QStringLiteral( "pointnet2_simple/logs/" );
}

// ------------------------------------------------------------------
// dataset paths
// ------------------------------------------------------------------

QString metadataJsonPath()
{
  return datasetsBaseDir() + QStringLiteral( "metadata/sample_params.json" );
}

QString dataRootPath()
{
  // Return without trailing separator — used as --data_root argument value
  QString d = datasetsBaseDir();
  if ( d.endsWith( QLatin1Char( '/' ) ) || d.endsWith( QLatin1Char( '\\' ) ) )
    d.chop( 1 );
  return d;
}

// ------------------------------------------------------------------
// settings dialog
// ------------------------------------------------------------------

void showSettingsDialog( QWidget *parent )
{
  QDialog dlg( parent );
  dlg.setWindowTitle( QStringLiteral( "PointNet 路径设置" ) );
  dlg.setMinimumWidth( 520 );

  // --- current values ---
  const QString curPython   = pythonExe();
  const QString curBase     = pointnetBaseDir();
  const QString curDataset  = datasetsBaseDir();
  const QString curClsModel = classifyModelName();
  const QString curRegPrefix = regressionModelPrefix();
  const QString curRegSuffix = regressionModelSuffix();
  const QString curPctSuffix = pctRegressionSuffix();

  // --- line edits ---
  auto *edtPython   = new QLineEdit( curPython, &dlg );
  auto *edtBase     = new QLineEdit( curBase, &dlg );
  auto *edtDataset  = new QLineEdit( curDataset, &dlg );
  auto *edtClsModel = new QLineEdit( curClsModel, &dlg );
  auto *edtRegPrefix = new QLineEdit( curRegPrefix, &dlg );
  auto *edtRegSuffix = new QLineEdit( curRegSuffix, &dlg );
  auto *edtPctSuffix = new QLineEdit( curPctSuffix, &dlg );

  auto makeBrowse = [&]( QLineEdit *edt, bool dirOnly ) {
    auto *btn = new QPushButton( QStringLiteral( "浏览..." ), &dlg );
    QObject::connect( btn, &QPushButton::clicked, [&dlg, edt, dirOnly]() {
      const QString path = dirOnly
        ? QFileDialog::getExistingDirectory( &dlg, QString(), edt->text() )
        : QFileDialog::getOpenFileName( &dlg, QString(), edt->text(),
                                        QStringLiteral( "Python (*.exe);;所有文件 (*)" ) );
      if ( !path.isEmpty() )
        edt->setText( path );
    } );
    return btn;
  };

  // --- layout ---
  auto *form = new QFormLayout;
  {
    auto *hb = new QHBoxLayout;
    hb->addWidget( edtPython );
    hb->addWidget( makeBrowse( edtPython, false ) );
    form->addRow( QStringLiteral( "Python 解释器:" ), hb );
  }
  {
    auto *hb = new QHBoxLayout;
    hb->addWidget( edtBase );
    hb->addWidget( makeBrowse( edtBase, true ) );
    form->addRow( QStringLiteral( "PointNet 根目录:" ), hb );
  }
  {
    auto *hb = new QHBoxLayout;
    hb->addWidget( edtDataset );
    hb->addWidget( makeBrowse( edtDataset, true ) );
    form->addRow( QStringLiteral( "数据集根目录:" ), hb );
  }
  // --- model version settings (no browse — these are relative names) ---
  {
    form->addRow( QStringLiteral( "分类模型名:" ), edtClsModel );
  }
  {
    form->addRow( QStringLiteral( "PCT 回归默认后缀:" ), edtPctSuffix );
    auto *pctHint = new QLabel(
      QStringLiteral( "  默认 _v2_neighbor (10/13 类最优)。CylinderDome/HalfCylinder/LHouse 自动用 _v2。" ), &dlg );
    pctHint->setStyleSheet( QStringLiteral( "color: #888; font-size: 11px;" ) );
    form->addRow( QString(), pctHint );
  }
  {
    auto *hb = new QHBoxLayout;
    hb->addWidget( edtRegPrefix );
    hb->addWidget( edtRegSuffix );
    form->addRow( QStringLiteral( "PointNeXt 回归 (前缀+后缀):" ), hb );
    auto *pnxHint = new QLabel(
      QStringLiteral( "  仅 PointNeXt 后端使用，PCT 忽略此项。" ), &dlg );
    pnxHint->setStyleSheet( QStringLiteral( "color: #888; font-size: 11px;" ) );
    form->addRow( QString(), pnxHint );
  }

  auto *lblHint = new QLabel(
    QStringLiteral( "修改后需重启插件才能生效。" ), &dlg );
  lblHint->setStyleSheet( QStringLiteral( "color: #888;" ) );

  auto *btnReset = new QPushButton( QStringLiteral( "恢复默认值" ), &dlg );
  QObject::connect( btnReset, &QPushButton::clicked, [&]() {
    edtPython->setText(    QStringLiteral( "E:/mambaforge/envs/pointnet_train/python.exe" ) );
    edtBase->setText(      QStringLiteral( "E:/pointnet" ) );
    edtDataset->setText(   QStringLiteral( "E:/pointnet/datasets_aug" ) );
    edtClsModel->setText(  QStringLiteral( "pct_cls_v2" ) );
    edtPctSuffix->setText( QStringLiteral( "_v2_neighbor" ) );
    edtRegPrefix->setText( QStringLiteral( "pointnext_reg_" ) );
    edtRegSuffix->setText( QStringLiteral( "_aux" ) );
  } );

  auto *btnBox = new QHBoxLayout;
  auto *btnOk     = new QPushButton( QStringLiteral( "确定" ), &dlg );
  auto *btnCancel = new QPushButton( QStringLiteral( "取消" ), &dlg );
  btnBox->addStretch();
  btnBox->addWidget( btnOk );
  btnBox->addWidget( btnCancel );

  QObject::connect( btnOk, &QPushButton::clicked, [&]() {
    QgsSettings s;
    s.setValue( QStringLiteral( "parammodeler/pythonExe" ),            edtPython->text() );
    s.setValue( QStringLiteral( "parammodeler/pointnetBase" ),         edtBase->text() );
    s.setValue( QStringLiteral( "parammodeler/datasetsBase" ),         edtDataset->text() );
    s.setValue( QStringLiteral( "parammodeler/classifyModelName" ),    edtClsModel->text() );
    s.setValue( QStringLiteral( "parammodeler/regressionModelPrefix" ), edtRegPrefix->text() );
    s.setValue( QStringLiteral( "parammodeler/regressionModelSuffix" ), edtRegSuffix->text() );
    s.setValue( QStringLiteral( "parammodeler/pctRegressionSuffix" ),   edtPctSuffix->text() );
    dlg.accept();
  } );
  QObject::connect( btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject );

  auto *root = new QVBoxLayout( &dlg );
  root->addLayout( form );
  root->addSpacing( 6 );
  root->addWidget( lblHint );
  root->addWidget( btnReset );
  root->addSpacing( 6 );
  root->addLayout( btnBox );

  dlg.exec();
}

} // namespace ParamModelerConfig
