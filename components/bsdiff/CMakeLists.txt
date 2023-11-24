set(SOURCE_FILES
                "src/7zFile.c"   
                "src/bspatch.c"
                "src/crc32.c"
                "src/LzFind.c"
                "src/lzma_decompress.c"
                "src/LzmaDec.c"
                "src/LzmaEnc.c"
#               "src/mylib.c"
                "src/user_interface.c"
                "src/vFile.c"
                )

idf_component_register(SRCS ${SOURCE_FILES}
                    INCLUDE_DIRS "inc"
                    PRIV_REQUIRES nvs_flash driver esp_https_ota app_update )
