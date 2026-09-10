file(GLOB HELIX_AAC_SRCS "src/*.c")

idf_component_register(
    SRCS ${HELIX_AAC_SRCS}
    INCLUDE_DIRS "src"
)

# ARDUINO=1 selects the generic-C paths in the RealNetworks source:
#   assembly.h -- 64-bit MADD/CLZ/etc. in plain C (no Xtensa asm exists in
#                 this codebase, so we can't use an arch-specific block)
#   aacdec.h   -- accepts the platform without an #error
#   buffers.c  -- pulls in <stdlib.h> instead of Helix's hlxclib/ variant
#   sbr.c      -- same, with <stdio.h> too
# Has to be PUBLIC because aacdec.h itself checks it, so any dependent that
# #includes aacdec.h (currently just main/audio/aac_decoder.c) would hit the
# "No platform defined" #error at the bottom of that #if/#elif chain
# without this flag on its own compile line.
target_compile_definitions(${COMPONENT_LIB} PUBLIC ARDUINO=1)

# Signal to firmware/main/audio/aac_decoder.c that the real decoder is
# linked in and it should compile against libhelix's aacdec.h instead of
# its "library missing" stub. PUBLIC so the main component picks it up.
target_compile_definitions(${COMPONENT_LIB} PUBLIC HELIX_AAC_AVAILABLE=1)

# The RealNetworks source is decades old and predates -Wall being routine.
# Silence the diagnostics that fire from every table (implicit widening on
# fixed-point tables, unused parameters in stubbed hooks, etc.) so a real
# warning in OUR code isn't drowned in library noise.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-sign-compare
    -Wno-implicit-fallthrough
    -Wno-shift-negative-value
    -Wno-maybe-uninitialized
)
