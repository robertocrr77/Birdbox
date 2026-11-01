idf_component_register(SRC_DIRS "libhelix-mp3" "."
    INCLUDE_DIRS "." "libhelix-mp3")
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -fwrapv)
