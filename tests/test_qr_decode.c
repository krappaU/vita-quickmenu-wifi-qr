// SPDX-License-Identifier: MIT
// Decodes QR images made by tests/make_qr.py (PGM files) the way the camera loop does, at the
// camera's resolutions, then parses the payload.
#include <stdlib.h>

#include "../src/qr_decode.h"
#include "../src/wifi_qr.h"
#include "check.h"

static unsigned char *read_pgm(const char *path, int *w, int *h) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    int maxv;
    if (fscanf(f, "P5 %d %d %d", w, h, &maxv) != 3) { fclose(f); return NULL; }
    fgetc(f);
    unsigned char *p = malloc((size_t)*w * *h);
    if (fread(p, 1, (size_t)*w * *h, f) != (size_t)*w * *h) { free(p); p = NULL; }
    fclose(f);
    return p;
}

int main(int argc, char **argv) {
    // argv: pairs of <image.pgm> <expected payload>
    for (int i = 1; i + 1 < argc; i += 2) {
        int w, h;
        unsigned char *img = read_pgm(argv[i], &w, &h);
        CHECK_TRUE(img != NULL);
        if (!img) continue;
        qr_decoder *d = qr_decoder_new(w, h);
        char payload[QR_PAYLOAD_MAX + 1];
        qr_stats st;
        memset(&st, 0, sizeof st);
        int n = qr_decode(d, img, w, payload, sizeof payload, &st);
        int seen = st.codes;
        if (strcmp(argv[i + 1], "-") == 0) { // no code expected
            CHECK_INT(n, 0);
        } else {
            CHECK_TRUE(seen >= 1); // the corner-square fallback can propose several grid sizes
            CHECK_TRUE(st.capstones >= 3);
            CHECK_INT(n, (int)strlen(argv[i + 1]));
            CHECK_STR(n > 0 ? payload : "", argv[i + 1]);
            wifi_cred c;
            if (strncmp(argv[i + 1], "WIFI:", 5) == 0)
                CHECK_INT(wifi_qr_parse(payload, (size_t)n, &c), WIFI_QR_OK);
        }
        qr_decoder_free(d);
        free(img);
    }
    return CHECK_DONE("qr_decode");
}
