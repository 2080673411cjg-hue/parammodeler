/***************************************************************************
  parammodeler_randomizer.h
  Random parameter generation for all primitive types
  -------------------
         begin                : July 2026
         copyright            : (C) 2026 by Chai
         email                : 2080673411@qq.com
 ***************************************************************************/

#ifndef PARAMMODELER_RANDOMIZER_H
#define PARAMMODELER_RANDOMIZER_H

class ParamModelerDock;

void randomizePrimitiveParams( ParamModelerDock *dock,
                               bool refreshPreview,
                               bool randomizePose );

#endif // PARAMMODELER_RANDOMIZER_H
