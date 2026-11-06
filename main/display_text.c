#include <string.h>
#include "display_text.h"

void display_ascii(const char *utf8, char *output, size_t capacity)
{
    if (!output || !capacity) return;
    size_t used = 0;
    const unsigned char *p = (const unsigned char *)(utf8 ? utf8 : "");
    const char *variants[] = {
        "ÀÁẠẢÃÂẦẤẬẨẪĂẰẮẶẲẴàáạảãâầấậẩẫăằắặẳẵ",
        "ÈÉẸẺẼÊỀẾỆỂỄèéẹẻẽêềếệểễ", "ÌÍỊỈĨìíịỉĩ",
        "ÒÓỌỎÕÔỒỐỘỔỖƠỜỚỢỞỠòóọỏõôồốộổỗơờớợởỡ",
        "ÙÚỤỦŨƯỪỨỰỬỮùúụủũưừứựửữ", "ỲÝỴỶỸỳýỵỷỹ", "Đđ",
    };
    const char bases[] = "AEIOUYD";
    while (*p && used + 1 < capacity) {
        char letter = ' ';
        if (*p < 128) {
            unsigned c = *p++;
            if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) letter = c;
        } else {
            unsigned length = (*p & 0xe0) == 0xc0 ? 2 : (*p & 0xf0) == 0xe0 ? 3 : (*p & 0xf8) == 0xf0 ? 4 : 1;
            char sequence[5] = {0};
            unsigned n = 0;
            while (n < length && p[n] && (!n || (p[n] & 0xc0) == 0x80)) { sequence[n] = p[n]; ++n; }
            // Combining marks have no glyph of their own (decomposed UTF-8).
            if (n == 2 && (p[0] == 0xcc || (p[0] == 0xcd && p[1] < 0xb0))) { p += n; continue; }
            if (n == length && length > 1) for (unsigned i = 0; i < sizeof(bases) - 1; ++i)
                if (strstr(variants[i], sequence)) { letter = bases[i]; break; }
            p += n ? n : 1;
        }
        if (letter != ' ' || (used && output[used - 1] != ' ')) output[used++] = letter;
    }
    while (used && output[used - 1] == ' ') --used;
    output[used] = 0;
}
