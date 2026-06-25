#include "geometry.h"
#include <math.h>

Point3D rotateY(Point3D p, float rad) {
    float c = cos(rad), s = sin(rad);
    return { p.x * c + p.z * s, p.y, -p.x * s + p.z * c };
}

Point3D rotateX(Point3D p, float rad) {
    float c = cos(rad), s = sin(rad);
    return { p.x, p.y * c - p.z * s, p.y * s + p.z * c };
}

Point3D sphereToCartesian(float latDeg, float lonDeg) {
    float lat = latDeg * M_PI / 180.0f;
    float lon = lonDeg * M_PI / 180.0f;
    return { cos(lat) * cos(lon), sin(lat), cos(lat) * sin(lon) };
}