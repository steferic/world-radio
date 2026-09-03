@echo off
idf.py -B build\n16r8 -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.n16r8" -DSDKCONFIG="build\n16r8\sdkconfig" %*