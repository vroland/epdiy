set(srcs 
    "JPEGDEC.cpp"
    "jpeg.c"
)
idf_component_register(SRCS ${srcs}      
                    REQUIRES "jpegdec"
                    INCLUDE_DIRS "include"
)