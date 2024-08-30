#ifndef ZERRCODES_H_
#define ZERRCODES_H_ 1

#define Z_OK 0
#define INVALID_COMP_METHOD (-1)
#define INVALID_WINDOW_SIZE (-2)
#define CORRUPT_ZLIB_HEADER (-3)
#define STREAM_TOO_SHORT (-4)
#define LEN_CHECK_FAIL (-5)
#define DICT_IS_USED (-6)
#define ILLEGAL_BTYPE (-7)
#define INVALID_MATCH_LEN (-8)
#define ADLER_CHECKSUM_ERR (-9)
#define FILE_ERROR (-10)
#define UNDEFINED_ERROR (-99)

inline const char *z_strerr(int code)
{
    switch (code) {
    case Z_OK:
        return "No error\n";
    case INVALID_COMP_METHOD:
        return "Invalid compression method (CM != 8)";
    case INVALID_WINDOW_SIZE:
        return "Invalid window size (>32KB)";
    case CORRUPT_ZLIB_HEADER:
        return "Corrupt stream header";
    case STREAM_TOO_SHORT:
        return "Unexpected end of stream";
    case LEN_CHECK_FAIL:
        return "One's complement of stored chunk length does not match"
               " NLEN";
    case DICT_IS_USED:
        return "Preset dictionaries not supported";
    case ILLEGAL_BTYPE:
        return "Illegal BTYPE of chunk (3)";
    case INVALID_MATCH_LEN:
        return "Invalid match distance (outside of stream)";
    case ADLER_CHECKSUM_ERR:
        return "Deflated data checksum does not match stored checksum";
    case FILE_ERROR:
        return "File I/O error";
    default:
        return "Unknown error";
    }
}


#endif // ZERRCODES_H_
