#ifndef NANOX_LOCAL_PCRE2_H_
#define NANOX_LOCAL_PCRE2_H_

#include <stddef.h>
#include <stdint.h>

#ifndef PCRE2_CODE_UNIT_WIDTH
#define PCRE2_CODE_UNIT_WIDTH 8
#endif

typedef struct pcre2_real_code_8 pcre2_code;
typedef struct pcre2_real_match_data_8 pcre2_match_data;
typedef const unsigned char *PCRE2_SPTR;
typedef unsigned char PCRE2_UCHAR;
typedef size_t PCRE2_SIZE;

#define PCRE2_ZERO_TERMINATED ((PCRE2_SIZE)-1)
#define PCRE2_UTF       0x00080000u
#define PCRE2_UCP       0x00020000u
#define PCRE2_MULTILINE 0x00000400u
#define PCRE2_CASELESS  0x00000008u

#ifdef __cplusplus
extern "C" {
#endif

#define pcre2_compile pcre2_compile_8
#define pcre2_match_data_create_from_pattern pcre2_match_data_create_from_pattern_8
#define pcre2_match pcre2_match_8
#define pcre2_get_ovector_pointer pcre2_get_ovector_pointer_8
#define pcre2_get_error_message pcre2_get_error_message_8
#define pcre2_code_free pcre2_code_free_8
#define pcre2_match_data_free pcre2_match_data_free_8

pcre2_code *pcre2_compile_8(const PCRE2_SPTR pattern,
                            PCRE2_SIZE length,
                            uint32_t options,
                            int *errorcode,
                            PCRE2_SIZE *erroroffset,
                            void *compile_context);

pcre2_match_data *pcre2_match_data_create_from_pattern_8(const pcre2_code *code,
                                                         void *general_context);

int pcre2_match_8(const pcre2_code *code,
                  PCRE2_SPTR subject,
                  PCRE2_SIZE length,
                  PCRE2_SIZE startoffset,
                  uint32_t options,
                  pcre2_match_data *match_data,
                  void *match_context);

PCRE2_SIZE *pcre2_get_ovector_pointer_8(pcre2_match_data *match_data);

int pcre2_get_error_message_8(int errorcode, PCRE2_UCHAR *buffer, PCRE2_SIZE buflen);

void pcre2_code_free_8(pcre2_code *code);
void pcre2_match_data_free_8(pcre2_match_data *match_data);

#ifdef __cplusplus
}
#endif

#endif /* NANOX_LOCAL_PCRE2_H_ */
