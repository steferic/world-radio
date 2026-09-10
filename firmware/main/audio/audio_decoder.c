#include "audio_decoder.h"

#include <string.h>
#include <strings.h>

audio_format_t audio_format_from_string(const char *s)
{
    if (s == NULL || s[0] == '\0') return AUDIO_FORMAT_UNKNOWN;
    if (strcasecmp(s, "mp3") == 0)  return AUDIO_FORMAT_MP3;
    if (strcasecmp(s, "aac") == 0)  return AUDIO_FORMAT_AAC;
    if (strcasecmp(s, "aacp") == 0) return AUDIO_FORMAT_AAC;
    if (strcasecmp(s, "aac+") == 0) return AUDIO_FORMAT_AAC;
    return AUDIO_FORMAT_UNKNOWN;
}

const char *audio_format_to_string(audio_format_t fmt)
{
    switch (fmt) {
        case AUDIO_FORMAT_MP3:     return "mp3";
        case AUDIO_FORMAT_AAC:     return "aac";
        case AUDIO_FORMAT_UNKNOWN: return "unknown";
    }
    return "unknown";
}

// Both MP3 and ADTS-AAC start with an 11- or 12-bit sync word made of set
// bits, packed into the first two bytes. What disambiguates them are the
// "layer" bits in byte[1]:
//   MP3 (MPEG-1/2 audio, layer III):  byte[0]=0xFF, byte[1]&0xE6 in {0xE2,0xE4,0xE6,0xF2,0xF4,0xF6} (layer bits != 00)
//   ADTS AAC:                          byte[0]=0xFF, byte[1]&0xF6 in {0xF0,0xF8,0xF1,0xF9} (layer bits == 00, marker bit set)
// We also skip a leading ID3v2 tag ("ID3") which is basically always MP3.
audio_format_t audio_format_sniff(const uint8_t *buf, size_t len)
{
    if (buf == NULL) return AUDIO_FORMAT_UNKNOWN;

    // ID3v2 tag -> the payload after it is (in practice) MP3.
    if (len >= 3 && buf[0] == 'I' && buf[1] == 'D' && buf[2] == '3') {
        return AUDIO_FORMAT_MP3;
    }

    // Walk forward a bit looking for the first byte that could be a sync
    // word. Real streams sometimes have a few junk bytes at the front, so
    // don't insist that offset 0 is the frame boundary.
    size_t scan = len > 64 ? 64 : len;
    for (size_t i = 0; i + 1 < scan; i++) {
        if (buf[i] != 0xFF) continue;
        uint8_t b1 = buf[i + 1];
        if ((b1 & 0xE0) != 0xE0) continue; // top 3 bits of byte 1 must be set

        // Bits 2..1 of byte[1] are the MPEG "layer" field.
        //   00 -> reserved/ADTS AAC marker
        //   01 -> layer III (MP3)
        //   10 -> layer II
        //   11 -> layer I
        uint8_t layer = (b1 >> 1) & 0x03;
        if (layer == 0) {
            return AUDIO_FORMAT_AAC;
        }
        return AUDIO_FORMAT_MP3;
    }
    return AUDIO_FORMAT_UNKNOWN;
}
