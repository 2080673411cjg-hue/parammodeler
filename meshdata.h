/***************************************************************************
  meshdata.h
  网格数据结构与工具函数
  -------------------
          begin                : Mar. 2026
          copyright            : (C) 2026 by Chai
          email                : 2080673411@qq.com
***************************************************************************/

/***************************************************************************
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 ***************************************************************************/
#ifndef MESHDATA_H
#define MESHDATA_H

#include <QVector>
#include <QVector3D>

// ============================================================
// MeshData：顶点 + 三角面片索引
// 供 PreviewGLWidget 渲染和 ExportOBJ 写文件共用
// ============================================================
struct MeshData
{
    QVector<QVector3D> vertices;  // 顶点列表
    QVector<int>       indices;   // 每3个为一个三角形（CCW 朝外）

    void clear()
    {
        vertices.clear();
        indices.clear();
    }

    bool isEmpty() const
    {
        return vertices.isEmpty() || indices.isEmpty();
    }

    // ---- 工具函数：添加三角形 ----
    void addTriangle( const QVector3D &a, const QVector3D &b, const QVector3D &c )
    {
        int base = vertices.size();
        vertices << a << b << c;
        indices  << base << base + 1 << base + 2;
    }

    // ---- 工具函数：四边形拆成两个三角形（a-b-c-d 逆时针） ----
    void addQuad( const QVector3D &a, const QVector3D &b,
                  const QVector3D &c, const QVector3D &d )
    {
        addTriangle( a, b, c );
        addTriangle( a, c, d );
    }
};

#endif // MESHDATA_H
