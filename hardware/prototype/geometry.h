#ifndef GEOMETRY_H
#define GEOMETRY_H

struct Point3D { float x, y, z; };

Point3D rotateY(Point3D p, float rad);
Point3D rotateX(Point3D p, float rad);
Point3D sphereToCartesian(float latDeg, float lonDeg);

#endif