#ifndef PARAMMODELER_PRIMITIVE_NAMES_H
#define PARAMMODELER_PRIMITIVE_NAMES_H

#include <QCoreApplication>
#include <QString>

namespace ParamModelerPrimitiveNames
{
struct Entry { const char *legacy; const char *name; const char *oldSlug; const char *slug; };
inline const Entry *entries()
{
  static const Entry values[] = {
    { "Cuboid", "FlatRoofHouse", "cuboid", "flatroof" },
    { "Cylinder", "RoundFlatRoofHouse", "cylinder", "roundflat" },
    { "LHouse", "LShapedHouse", "lhouse", "lshaped" },
    { "ConeCylinder", "ConicalRoofHouse", "conecylinder", "conicalroof" },
    { "GabledRoof", "GableRoofHouse", "gabledroof", "gableroof" },
    { "PyramidRoof", "PyramidalRoofHouse", "pyramidroof", "pyramidalroof" },
    { "TruncatedPyramidRoof", "FlatTopHipRoofHouse", "truncatedpyramid", "flattophip" },
    { "HalfCylinderRoof", "BarrelRoofHouse", "halfcylinder", "barrelroof" },
    { "CylinderDome", "DomeRoofHouse", "cylinderdome", "domeroof" },
    { "IndentedCuboid", "RecessedRoofHouse", "indentedcuboid", "recessedroof" },
    { "AsymmetricGableHouse", "ShortRidgeHipRoofHouse", "asymgable", "shortridgehip" },
    { "FourStageRoundTower", "CurvedRoofRoundHouse", "fourstage", "curvedround" },
    { "TwoGableHouses", "AngledGableHouse", "twogable", "angledgable" },
  };
  return values;
}
inline QString canonical( const QString &name )
{
  if ( name == QLatin1String( "CylinderHemisphere" ) ) return QStringLiteral( "DomeRoofHouse" );
  for ( int i = 0; i < 13; ++i )
    if ( name == QLatin1String( entries()[i].legacy ) || name == QLatin1String( entries()[i].name ) )
      return QString::fromLatin1( entries()[i].name );
  return name;
}
inline QString legacy( const QString &name )
{
  const QString type = canonical( name );
  for ( int i = 0; i < 13; ++i )
    if ( type == QLatin1String( entries()[i].name ) ) return QString::fromLatin1( entries()[i].legacy );
  return name;
}
inline QString slug( const QString &name, bool oldNames )
{
  const QString type = canonical( name );
  for ( int i = 0; i < 13; ++i )
    if ( type == QLatin1String( entries()[i].name ) )
      return QString::fromLatin1( oldNames ? entries()[i].oldSlug : entries()[i].slug );
  return QString();
}
inline bool legacySuffix( const QString &suffix )
{
  if ( !suffix.startsWith( QLatin1String( "_v" ) ) ) return false;
  QString digits;
  for ( const QChar c : suffix.mid( 2 ) )
  {
    if ( !c.isDigit() ) break;
    digits += c;
  }
  bool ok = false;
  const int version = digits.toInt( &ok );
  return ok && version <= 9;
}
inline QString display( const QString &name )
{
  return QCoreApplication::translate( "ParamModelerDock", canonical( name ).toUtf8().constData() );
}
}
#endif
