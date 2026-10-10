# Converts a file into a C array: cmake -DIN=... -DOUT=... -DSYM=... -P embed.cmake
file(READ "${IN}" HEX_CONTENT HEX)
string(LENGTH "${HEX_CONTENT}" HEX_LEN)
math(EXPR BYTE_LEN "${HEX_LEN} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," ARRAY "${HEX_CONTENT}")
file(WRITE "${OUT}" "#include <stddef.h>\nconst unsigned char ${SYM}[] = {${ARRAY}0};\nconst size_t ${SYM}_len = ${BYTE_LEN};\n")
