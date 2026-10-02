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
#include <QComboBox>
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

QString ptv3PythonExe()
{
  return setting( QStringLiteral( "parammodeler/ptv3PythonExe" ),
                  QStringLiteral( "E:/mambaforge/envs/ptv3_cpu/python.exe" ) );
}

QString ptv3ServerUrl()
{
  const QString profile = ptv3ServerProfile();
  QString url = profile == QStringLiteral( "tailscale" )
    ? ptv3TailscaleServerUrl()
    : ptv3LanServerUrl();
  while ( url.endsWith( QLatin1Char( '/' ) ) )
    url.chop( 1 );
  return url;
}

QString ptv3LanServerUrl()
{
  return setting( QStringLiteral( "parammodeler/ptv3LanServerUrl" ),
                  setting( QStringLiteral( "parammodeler/ptv3ServerUrl" ),
                           QStringLiteral( "http://192.168.1.113:8008" ) ) ).trimmed();
}

QString ptv3TailscaleServerUrl()
{
  return setting( QStringLiteral( "parammodeler/ptv3TailscaleServerUrl" ),
                  QStringLiteral( "http://100.104.75.49:8008" ) ).trimmed();
}

QString ptv3ServerProfile()
{
  const QString profile = setting( QStringLiteral( "parammodeler/ptv3ServerProfile" ),
                                   QStringLiteral( "lan" ) ).trimmed().toLower();
  return profile == QStringLiteral( "tailscale" ) ? QStringLiteral( "tailscale" ) : QStringLiteral( "lan" );
}

QString pythonExeForBackend( PointNetBackend backend )
{
  if ( backend == PointNetBackend::PTv3 )
    return ptv3PythonExe();
  return pythonExe();
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
                  QStringLiteral( "pct_cls_v4" ) );
}

QString regressionModelPrefix()
{
  return setting( QStringLiteral( "parammodeler/regressionModelPrefix" ),
                  QStringLiteral( "pointnext_reg_" ) );
}

QString regressionModelSuffix()
{
  return setting( QStringLiteral( "parammodeler/regressionModelSuffix" ),
                  QStringLiteral( "_v2" ) );
}

QString pctClassifyLogDir()
{
  return setting( QStringLiteral( "parammodeler/pctClassifyLogDir" ),
                  pointnetBaseDir() + QStringLiteral( "pct_simple/logs/pct_cls_v4" ) );
}

QString pctRegressionSuffix()
{
  return setting( QStringLiteral( "parammodeler/pctRegressionSuffix" ),
                  QStringLiteral( "_v4_normals" ) );
}

QString ptv3ClassifyLogDir()
{
  return setting( QStringLiteral( "parammodeler/ptv3ClassifyLogDir" ),
                  pointnetBaseDir() + QStringLiteral( "ptv3_simple/logs/ptv3_cls_v2" ) );
}

QString ptv3RegressionSuffix()
{
  return setting( QStringLiteral( "parammodeler/ptv3RegressionSuffix" ),
                  QStringLiteral( "_v2_normals" ) );
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
    case PointNetBackend::PTv3:      return base + QStringLiteral( "ptv3_simple/main.py" );
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
    case PointNetBackend::PCT:       return pctClassifyLogDir();
    case PointNetBackend::PTv3:      return ptv3ClassifyLogDir();
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
  if ( backend == PointNetBackend::PTv3 )
    return base + QStringLiteral( "ptv3_simple/main_reg.py" );
  return base + QStringLiteral( "pointnet2_simple/main_reg.py" );
}

QString regressionLogBase( PointNetBackend backend )
{
  const QString base = pointnetBaseDir();
  if ( backend == PointNetBackend::PointNeXt )
    return base + QStringLiteral( "pointnext_simple/logs/" );
  if ( backend == PointNetBackend::PCT )
    return base + QStringLiteral( "pct_simple/logs/" );
  if ( backend == PointNetBackend::PTv3 )
    return base + QStringLiteral( "ptv3_simple/logs/" );
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
  const QString curPtv3Python = ptv3PythonExe();
  const QString curPtv3LanServer = ptv3LanServerUrl();
  const QString curPtv3TailscaleServer = ptv3TailscaleServerUrl();
  const QString curPtv3ServerProfile = ptv3ServerProfile();
  const QString curBase     = pointnetBaseDir();
  const QString curDataset  = datasetsBaseDir();
  const QString curPctClsLog = pctClassifyLogDir();
  const QString curPctSuffix = pctRegressionSuffix();
  const QString curPtv3ClsLog = ptv3ClassifyLogDir();
  const QString curPtv3Suffix = ptv3RegressionSuffix();

  // --- line edits ---
  auto *edtPython   = new QLineEdit( curPython, &dlg );
  auto *edtPtv3Python = new QLineEdit( curPtv3Python, &dlg );
  auto *comboPtv3ServerProfile = new QComboBox( &dlg );
  comboPtv3ServerProfile->addItem( QStringLiteral( "实验室局域网" ), QStringLiteral( "lan" ) );
  comboPtv3ServerProfile->addItem( QStringLiteral( "Tailscale" ), QStringLiteral( "tailscale" ) );
  const int serverProfileIndex = comboPtv3ServerProfile->findData( curPtv3ServerProfile );
  comboPtv3ServerProfile->setCurrentIndex( serverProfileIndex >= 0 ? serverProfileIndex : 0 );
  auto *edtPtv3LanServer = new QLineEdit( curPtv3LanServer, &dlg );
  auto *edtPtv3TailscaleServer = new QLineEdit( curPtv3TailscaleServer, &dlg );
  auto *edtBase     = new QLineEdit( curBase, &dlg );
  auto *edtDataset  = new QLineEdit( curDataset, &dlg );
  auto *edtPctClsLog = new QLineEdit( curPctClsLog, &dlg );
  auto *edtPctSuffix = new QLineEdit( curPctSuffix, &dlg );
  auto *edtPtv3ClsLog = new QLineEdit( curPtv3ClsLog, &dlg );
  auto *edtPtv3Suffix = new QLineEdit( curPtv3Suffix, &dlg );

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
    hb->addWidget( edtPtv3Python );
    hb->addWidget( makeBrowse( edtPtv3Python, false ) );
    form->addRow( QStringLiteral( "PTv3 Python 解释器:" ), hb );
  }
  {
    form->addRow( QStringLiteral( "PTv3 服务选择:" ), comboPtv3ServerProfile );
  }
  {
    form->addRow( QStringLiteral( "PTv3 局域网地址:" ), edtPtv3LanServer );
  }
  {
    form->addRow( QStringLiteral( "PTv3 Tailscale 地址:" ), edtPtv3TailscaleServer );
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
  {
    auto *hb = new QHBoxLayout;
    hb->addWidget( edtPctClsLog );
    hb->addWidget( makeBrowse( edtPctClsLog, true ) );
    form->addRow( QStringLiteral( "PCT 分类模型目录:" ), hb );
    form->addRow( QStringLiteral( "PCT 回归默认后缀:" ), edtPctSuffix );
    auto *pctHint = new QLabel(
      QStringLiteral( "  作为兜底后缀使用；PCT 会优先按基元类型选择 v4/v5/v6 最佳模型。" ), &dlg );
    pctHint->setStyleSheet( QStringLiteral( "color: #888; font-size: 11px;" ) );
    form->addRow( QString(), pctHint );
  }
  {
    auto *hb = new QHBoxLayout;
    hb->addWidget( edtPtv3ClsLog );
    hb->addWidget( makeBrowse( edtPtv3ClsLog, true ) );
    form->addRow( QStringLiteral( "PTv3 分类模型目录:" ), hb );
    form->addRow( QStringLiteral( "PTv3 回归后缀:" ), edtPtv3Suffix );
    auto *ptv3Hint = new QLabel(
      QStringLiteral( "  回归目录按 PointNet 根目录下的 ptv3_simple/logs/ptv3_reg_<类别短名><后缀> 查找。" ), &dlg );
    ptv3Hint->setStyleSheet( QStringLiteral( "color: #888; font-size: 11px;" ) );
    form->addRow( QString(), ptv3Hint );
  }

  auto *lblHint = new QLabel(
    QStringLiteral( "保存后下次推理生效；若 Python/模型路径仍异常，可重启插件后再试。" ), &dlg );
  lblHint->setStyleSheet( QStringLiteral( "color: #888;" ) );

  auto *btnReset = new QPushButton( QStringLiteral( "恢复默认值" ), &dlg );
  QObject::connect( btnReset, &QPushButton::clicked, [&]() {
    edtPython->setText(    QStringLiteral( "E:/mambaforge/envs/pointnet_train/python.exe" ) );
    edtPtv3Python->setText( QStringLiteral( "E:/mambaforge/envs/ptv3_cpu/python.exe" ) );
    comboPtv3ServerProfile->setCurrentIndex( 0 );
    edtPtv3LanServer->setText( QStringLiteral( "http://192.168.1.113:8008" ) );
    edtPtv3TailscaleServer->setText( QStringLiteral( "http://100.104.75.49:8008" ) );
    edtBase->setText(      QStringLiteral( "E:/pointnet" ) );
    edtDataset->setText(   QStringLiteral( "E:/pointnet/datasets_aug" ) );
    edtPctClsLog->setText( QStringLiteral( "E:/pointnet/pct_simple/logs/pct_cls_v4" ) );
    edtPctSuffix->setText( QStringLiteral( "_v4_normals" ) );
    edtPtv3ClsLog->setText( QStringLiteral( "E:/pointnet/ptv3_simple/logs/ptv3_cls_v2" ) );
    edtPtv3Suffix->setText( QStringLiteral( "_v2_normals" ) );
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
    s.setValue( QStringLiteral( "parammodeler/ptv3PythonExe" ),        edtPtv3Python->text() );
    s.setValue( QStringLiteral( "parammodeler/ptv3ServerProfile" ),    comboPtv3ServerProfile->currentData().toString() );
    s.setValue( QStringLiteral( "parammodeler/ptv3LanServerUrl" ),     edtPtv3LanServer->text() );
    s.setValue( QStringLiteral( "parammodeler/ptv3TailscaleServerUrl" ), edtPtv3TailscaleServer->text() );
    s.setValue( QStringLiteral( "parammodeler/pointnetBase" ),         edtBase->text() );
    s.setValue( QStringLiteral( "parammodeler/datasetsBase" ),         edtDataset->text() );
    s.setValue( QStringLiteral( "parammodeler/pctClassifyLogDir" ),    edtPctClsLog->text() );
    s.setValue( QStringLiteral( "parammodeler/pctRegressionSuffix" ),   edtPctSuffix->text() );
    s.setValue( QStringLiteral( "parammodeler/ptv3ClassifyLogDir" ),    edtPtv3ClsLog->text() );
    s.setValue( QStringLiteral( "parammodeler/ptv3RegressionSuffix" ),   edtPtv3Suffix->text() );
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
