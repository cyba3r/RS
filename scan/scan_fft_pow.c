
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <complex.h>


typedef unsigned char  ui8_t;
typedef unsigned short ui16_t;
typedef unsigned int   ui32_t;
typedef short i16_t;
typedef int   i32_t;


static int option_verbose = 0,  // ausfuehrliche Anzeige
           option_silent = 0,
           wavloaded = 0;
static int wav_channel = 0;     // audio channel: left


static int sample_rate = 0, bits_sample = 0, channels = 0;
static int wav_ch = 0;  // 0: links bzw. mono; 1: rechts

static unsigned int sample;

float complex *buffer  = NULL;

void *bufIQ;

/* ------------------------------------------------------------------------------------ */

typedef struct {
    int sr;       // sample_rate
    int LOG2N;
    int N;
    int N2;
    float *xn;
    float complex  *ew;
    float complex  *Fm;
    float complex  *X;
    float complex  *Z;
    float complex  *cx;
    float complex  *win; // float real
} dft_t;


dft_t DFT;

static float *db, *sum_db, *intdb;
static float *avg, *peak;

static void raw_dft(dft_t *dft, float complex *Z) {
    int s, l, l2, i, j, k;
    float complex  w1, w2, T;

    j = 1;
    for (i = 1; i < dft->N; i++) {
        if (i < j) {
            T = Z[j-1];
            Z[j-1] = Z[i-1];
            Z[i-1] = T;
        }
        k = dft->N/2;
        while (k < j) {
            j = j - k;
            k = k/2;
        }
        j = j + k;
    }

    for (s = 0; s < dft->LOG2N; s++) {
        l2 = 1 << s;
        l  = l2 << 1;
        w1 = (float complex)1.0;
        w2 = dft->ew[s]; // cexp(-I*M_PI/(float)l2)
        for (j = 1; j <= l2; j++) {
            for (i = j; i <= dft->N; i += l) {
                k = i + l2;
                T = Z[k-1] * w1;
                Z[k-1] = Z[i-1] - T;
                Z[i-1] = Z[i-1] + T;
            }
            w1 = w1 * w2;
        }
    }
}

static void cdft(dft_t *dft, float complex *z, float complex *Z) {
    int i;
    for (i = 0; i < dft->N; i++)  Z[i] = z[i];
    raw_dft(dft, Z);
}

static void rdft(dft_t *dft, float *x, float complex *Z) {
    int i;
    for (i = 0; i < dft->N; i++)  Z[i] = (float complex)x[i];
    raw_dft(dft, Z);
}

static void Nidft(dft_t *dft, float complex *Z, float complex *z) {
    int i;
    for (i = 0; i < dft->N; i++)  z[i] = conj(Z[i]);
    raw_dft(dft, z);
    // idft():
    // for (i = 0; i < dft->N; i++)  z[i] = conj(z[i])/(float)dft->N; // hier: z reell
}

static float bin2freq(dft_t *dft, int k) {
    float fq = k / (float)dft->N;
    if ( fq >= 0.5) fq -= 1.0;
    return fq*dft->sr;
}

static float bin2fq(dft_t *dft, int k) {
    float fq = k / (float)dft->N;
    if ( fq >= 0.5) fq -= 1.0;
    return fq;
}

static float freq2bin(dft_t *dft, int f) {
    return  f/(float)dft->sr * dft->N;
}


/* ------------------------------------------------------------------------------------ */

static int dft_window(dft_t *dft, int w) {
    int n;

    if (w < 0 || w > 3) return -1;

    for (n = 0; n < dft->N2; n++) {
        switch (w)
        {
            case 0: // (boxcar)
                    dft->win[n] = 1.0;
                    break;
            case 1: // Hann
                    dft->win[n] = 0.5 * ( 1.0 - cos(2*M_PI*n/(float)(dft->N2-1)) );
                    break ;
            case 2: // Hamming
                    dft->win[n] = 25/46.0 + (1.0 - 25/46.0)*cos(2*M_PI*n / (float)(dft->N2-1));
                    break ;
            case 3: // Blackmann
                    dft->win[n] =  7938/18608.0
                                 - 9240/18608.0*cos(2*M_PI*n / (float)(dft->N2-1))
                                 + 1430/18608.0*cos(4*M_PI*n / (float)(dft->N2-1));
                    break ;
        }
    }
    while (n < dft->N) dft->win[n++] = 0.0;

    return 0;
}

//static double ilog102 = 0.434294482/2.0; // log(10)/2
static void db_power(dft_t *dft, float complex Z[], float db[]) {  // iq-samples/V [-1..1]
    int i;                                        // dBw = 2*dBv, P=c*U*U
    for (i = 0; i < dft->N; i++) {                // dBw = 2*10*log10(V/V0)
        //db[i] = 10.0*log10(creal(Z[i])*creal(Z[i])+cimag(Z[i])*cimag(Z[i])+1e-20);
        db[i] = 20.0 * log10(cabs(Z[i])/dft->N2+1e-20);   // 20log10(Z/N)
    }
}


static int init_dft(dft_t *dft) {
    int i, k, n;
    float normM = 0;

    bufIQ  = calloc(2*(dft->N+2), 2);  if (bufIQ == NULL) return -1;
    buffer = calloc(dft->N+1, sizeof(float complex));  if (buffer == NULL) return -1;

    dft->xn = calloc(dft->N+1,   sizeof(float complex));  if (dft->xn == NULL) return -1;
    dft->Z  = calloc(dft->N+1,   sizeof(float complex));  if (dft->Z  == NULL) return -1;
    dft->ew = calloc(dft->LOG2N, sizeof(float complex));  if (dft->ew == NULL) return -1;

    dft->win = calloc(dft->N+1, sizeof(float complex)); if (dft->win == NULL) return -1;
    dft->N2 = dft->N;
    dft_window(dft, 1);

    normM = 0;
    for (i = 0; i < dft->N2; i++)  normM += dft->win[i]*dft->win[i];
    //normM = sqrt(normM);

    for (n = 0; n < dft->LOG2N; n++) {
        k = 1 << n;
        dft->ew[n] = cexp(-I*M_PI/(double)k);
    }

    db     = calloc(dft->N+1, sizeof(float));  if (db == NULL) return -1;
    sum_db = calloc(dft->N+1, sizeof(float));  if (sum_db == NULL) return -1;
    intdb  = calloc(dft->N+1, sizeof(float));  if (intdb == NULL) return -1;
    avg    = calloc(dft->N+1, sizeof(float));  if (avg == NULL) return -1;
    peak   = calloc(dft->N+1, sizeof(float));  if (peak == NULL) return -1;

    return 0;
}


static void end_dft(dft_t *dft) {
    if (bufIQ)  { free(bufIQ);  bufIQ  = NULL; }
    if (buffer) { free(buffer); buffer = NULL; }
    if (dft->xn)  { free(dft->xn);  dft->xn  = NULL; }
    if (dft->Z)   { free(dft->Z);   dft->Z   = NULL; }
    if (dft->ew)  { free(dft->ew);  dft->ew  = NULL; }
    if (dft->win) { free(dft->win); dft->win = NULL; }
    if (db)     { free(db);     db     = NULL; }
    if (sum_db) { free(sum_db); sum_db = NULL; }
    if (intdb)  { free(intdb);  intdb  = NULL; }
    if (avg)    { free(avg);    avg    = NULL; }
    if (peak)   { free(peak);   peak   = NULL; }
}


/* ------------------------------------------------------------------------------------ */

static int findstr(char *buff, char *str, int pos) {
    int i;
    for (i = 0; i < 4; i++) {
        if (buff[(pos+i)%4] != str[i]) break;
    }
    return i;
}

static int read_wav_header(FILE *fp, int wav_channel) {
    char txt[4+1] = "\0\0\0\0";
    unsigned char dat[4];
    int byte, p=0;

    if (fread(txt, 1, 4, fp) < 4) return -1;
    if (strncmp(txt, "RIFF", 4)) return -1;
    if (fread(txt, 1, 4, fp) < 4) return -1;
    // pos_WAVE = 8L
    if (fread(txt, 1, 4, fp) < 4) return -1;
    if (strncmp(txt, "WAVE", 4)) return -1;
    // pos_fmt = 12L
    for ( ; ; ) {
        if ( (byte=fgetc(fp)) == EOF ) return -1;
        txt[p % 4] = byte;
        p++; if (p==4) p=0;
        if (findstr(txt, "fmt ", p) == 4) break;
    }
    if (fread(dat, 1, 4, fp) < 4) return -1;
    if (fread(dat, 1, 2, fp) < 2) return -1;

    if (fread(dat, 1, 2, fp) < 2) return -1;
    channels = dat[0] + (dat[1] << 8);

    if (fread(dat, 1, 4, fp) < 4) return -1;
    memcpy(&sample_rate, dat, 4); //sample_rate = dat[0]|(dat[1]<<8)|(dat[2]<<16)|(dat[3]<<24);

    if (fread(dat, 1, 4, fp) < 4) return -1;
    if (fread(dat, 1, 2, fp) < 2) return -1;
    //byte = dat[0] + (dat[1] << 8);

    if (fread(dat, 1, 2, fp) < 2) return -1;
    bits_sample = dat[0] + (dat[1] << 8);

    // pos_dat = 36L + info
    for ( ; ; ) {
        if ( (byte=fgetc(fp)) == EOF ) return -1;
        txt[p % 4] = byte;
        p++; if (p==4) p=0;
        if (findstr(txt, "data", p) == 4) break;
    }
    if (fread(dat, 1, 4, fp) < 4) return -1;


    fprintf(stderr, "sample_rate: %d\n", sample_rate);
    fprintf(stderr, "bits       : %d\n", bits_sample);
    fprintf(stderr, "channels   : %d\n", channels);

    if (wav_channel >= 0  &&  wav_channel < channels) wav_ch = wav_channel;
    else wav_ch = 0;
    fprintf(stderr, "channel-In : %d\n", wav_ch+1);

    if ((bits_sample != 8) && (bits_sample != 16)) return -1;

    return 0;
}


static int read_bufIQ(dft_t *dft, FILE *fp) {
    int len;

    len = fread( bufIQ, bits_sample/8, 2*dft->N2, fp);

    if ( len != 2*dft->N2) {
        while (len < 2*dft->N2) {
            //bufIQ[len] = 0;
            len++;
        }
        return EOF;
    }

    return 0;
}

static int bufIQ2complex(dft_t *dft) {
    int i;
    float complex z;
    unsigned char *buf8;
    short *buf16;

    if (bits_sample ==  8) {
        buf8 = bufIQ;
        for (i = 0; i < dft->N2; i++) {
            z = buf8[2*i]-128.0 + I*(buf8[2*i+1]-128.0);
            z /= 128.0;
            buffer[i] = z;
        }
    }
    else { // bits_sample == 16
        buf16 = bufIQ;
        for (i = 0; i < dft->N2; i++) {
            z = buf16[2*i] + I*buf16[2*i+1];
            z /= 128.0*256.0;
            buffer[i] = z;
        }
    }

    return 0;
}

static int f32read_sample(FILE *fp, float *s) {
    int i;
    short b = 0;

    for (i = 0; i < channels; i++) {

        if (fread( &b, bits_sample/8, 1, fp) != 1) return EOF;

        if (i == wav_ch) {  // i = 0: links bzw. mono
            //if (bits_sample ==  8)  sint = b-128;   // 8bit: 00..FF, centerpoint 0x80=128
            //if (bits_sample == 16)  sint = (short)b;

            if (bits_sample ==  8) { b -= 128; }
            *s = b/128.0;
            if (bits_sample == 16) { *s /= 256.0; }
        }
    }

    return 0;
}

/* ------------------------------------------------------------------------------------ */


int main(int argc, char **argv) {

    FILE *OUT = stderr;
    FILE *fpout = stdout;
    FILE *fp = NULL;
    char *prgnam = NULL;
    char *filename = NULL;

    int mn = 0; // 0: N = M

    int j, n, k;
    float tl = 4.0;

    float dx;
    int dn;
    //float avg = 0.0;
    float dev = 0.0;
    float sympeak = 0.0;
    float sympeak2 = 0.0;
    float globmin = 0.0;
    float globavg = 0.0;


#ifdef CYGWIN
    _setmode(fileno(stdin), _O_BINARY);  // _setmode(_fileno(stdin), _O_BINARY);
#endif
    setbuf(stdout, NULL);

    prgnam = argv[0];
    ++argv;
    while ((*argv) && (!wavloaded)) {
        if      ( (strcmp(*argv, "-h") == 0) || (strcmp(*argv, "--help") == 0) ) {
            fprintf(stderr, "%s [options] audio.wav\n", prgnam);
            fprintf(stderr, "  options:\n");
            //fprintf(stderr, "       -v, --verbose\n");
            return 0;
        }
        else if ( (strcmp(*argv, "-v") == 0) || (strcmp(*argv, "--verbose") == 0) ) {
            option_verbose = 1;
        }
        else if ( (strcmp(*argv, "-s") == 0) || (strcmp(*argv, "--silent") == 0) ) {
            option_silent = 1;
        }
        else if ( (strcmp(*argv, "-t") == 0) || (strcmp(*argv, "--time") == 0) ) {
            ++argv;
            if (*argv) tl = atof(*argv);
            else return -1;
        }
        else {
            if (strcmp(*argv, "-") == 0) {
                if (argv[1] == NULL) return -1; else sample_rate = atoi(argv[1]);
                if (argv[2] == NULL) return -1; else bits_sample = atoi(argv[2]);
                channels = 2;
                if (argv[3] == NULL) fp = stdin;
                else {
                    fp = fopen(argv[3], "rb");
                    if (fp == NULL) {
                        fprintf(stderr, "%s konnte nicht geoeffnet werden\n", *argv);
                        return -1;
                    }
                }
                wavloaded = 2;
            }
            else {
                fp = fopen(*argv, "rb");
                if (fp == NULL) {
                    fprintf(stderr, "%s konnte nicht geoeffnet werden\n", *argv);
                    return -1;
                }
                wavloaded = 1;
            }
        }
        ++argv;
    }
    if (!wavloaded) fp = stdin;


    if (wavloaded < 2) {
        j = read_wav_header(fp, wav_channel);
        if ( j < 0 ) {
            fclose(fp);
            fprintf(stderr, "error: wav header\n");
            return -1;
        }
    }

    DFT.sr = sample_rate;
    DFT.LOG2N = 14; // 2^12=4096: 300-400Hz bins, 2^14=16384: 75-100 Hz
    mn = 0;
    DFT.N2 = 1 << DFT.LOG2N;
    if (DFT.N2 > DFT.sr/2) {
        DFT.LOG2N = 0;
        while ( (1 << (DFT.LOG2N+1)) < DFT.sr/2 ) DFT.LOG2N++;
        DFT.N2 = 1 << DFT.LOG2N;
    }
    DFT.N = DFT.N2 << mn;
    DFT.LOG2N += mn;

    init_dft(&DFT);

    if (option_verbose) fprintf(stderr, "M: %d\n", DFT.N2);


    //memset(sum_db, 0, N*sizeof(float)); // calloc()

    sample = 0;
    n = 0;
    while ( read_bufIQ(&DFT, fp) != EOF ) {

        bufIQ2complex(&DFT);
        sample += DFT.N2;

        if (sample % DFT.N2 == 0)
        {
            double complex dc = 0;
            for (j = 0; j < DFT.N; j++) {
                DFT.Z[j] = buffer[j % DFT.N];
                dc += DFT.Z[j];
            }
            dc /= 0.99*DFT.N;
            //dc = 0;

            for (j = 0; j < DFT.N; j++) {
                DFT.Z[j] = buffer[j % DFT.N] - dc;
            }
///*
            for (j = 0; j < DFT.N2; j++) {
                DFT.Z[j] *= DFT.win[j];
            }
            while (j < DFT.N) DFT.Z[j++] = 0.0;
//*/
            raw_dft(&DFT, DFT.Z);
            db_power(&DFT, DFT.Z, db);

            for (j = 0; j < DFT.N; j++) sum_db[j] += db[j];

            n++;

            if (sample > tl*DFT.sr) break;
        }
    }
    if (option_verbose) fprintf(stderr, "n=%d\n", n);

    if (option_verbose == 0) {
        OUT = stdout;
        fpout = fopen("db.txt", "wb");
        if (fpout == NULL) return -1;
    } else {
        OUT = stderr;
        fpout = stdout;
    }


    globmin = 0.0;
    globavg = 0.0;
    float db_spike3 = 10.0;
    int spike_wl3 = 3; //freq2bin(&DFT, 200); // 3 // 200 Hz
    int spike_wl5 = 5; //freq2bin(&DFT, 200); // 3 // 200 Hz
    float db_spike1 = 15.0;
    int spike_wl1 = 1; //freq2bin(&DFT, 200); // 3 // 200 Hz
    //fprintf(stderr, "sp_wl: %d\n", spike_wl);

    dx = bin2freq(&DFT, 1);
    dn = 2*(int)(2400.0/dx)+1; // (odd/symmetric) integration width: 4800+dx Hz
    if (option_verbose) fprintf(stderr, "dn = %d\n", dn);
    for (j = 0; j < DFT.N; j++) sum_db[j] /= (float)n;

    // dc-spike (N-1,)N,0,1(,2): subtract mean/avg
    // spikes in general:
    for (j = 0; j < DFT.N; j++) {
        if ( sum_db[j] - sum_db[(j-spike_wl5+DFT.N)%DFT.N] > db_spike3
          && sum_db[j] - sum_db[(j-spike_wl3+DFT.N)%DFT.N] > db_spike3
          && sum_db[j] - sum_db[(j+spike_wl3+DFT.N)%DFT.N] > db_spike3
          && sum_db[j] - sum_db[(j+spike_wl5+DFT.N)%DFT.N] > db_spike3
           ) {
            sum_db[j] = (sum_db[(j-spike_wl3+DFT.N)%DFT.N]+sum_db[(j+spike_wl3+DFT.N)%DFT.N])/2.0;
        }
    }

    for (j = 0; j < DFT.N; j++) {
        float sum = 0.0;
        for (n = j-(dn-1)/2; n <= j+(dn-1)/2; n++) sum += sum_db[(n + DFT.N) % DFT.N];
        sum /= (float)dn;
        intdb[j] = sum;
        globavg += sum; // <=> sum_db[j];
        if (sum < globmin) globmin = sum;
    }
    globavg /= (float)DFT.N;

    if (option_verbose) fprintf(stderr, "avg=%.2f\n", globavg);
    if (option_verbose) fprintf(stderr, "min=%.2f\n", globmin);

    int dn2 = 2*dn+1;
    int dn3 = (int)(4000.0/dx); // (odd/symmetric) integration width: +/-4000 Hz

    int delay = (int)(24000.0/dx); // 16000
    float *bufavg = calloc(delay+1, sizeof(float)); if (bufavg == NULL) return -1;
    float *bufpeak = calloc(delay+1, sizeof(float)); if (bufpeak == NULL) return -1;
    k = 0;
    int mag = 0;
    int mag0 = 0;
    float max_db_loc = 0.0;
    int   max_db_idx = 0;
    for (j = DFT.N/2; j < DFT.N/2 + DFT.N; j++) {
        float x = intdb[(j+delay) % DFT.N];
        float a = 0.0;

        if (1 || option_verbose) fprintf(fpout, "%9.6f ; %9.1f ; %10.4f", bin2fq(&DFT, j % DFT.N), bin2freq(&DFT, j % DFT.N), sum_db[j % DFT.N]);
        if (1 || option_verbose) fprintf(fpout, " ; %10.4f", intdb[j % DFT.N]);

        a = 0.0;
        for (n = j-(dn2-1)/2; n <= j+(dn2-1)/2; n++) a += intdb[n % DFT.N];
        a /= (float)dn2;
        bufavg[k % delay] = a;

        dev = 0.0;
        for (n = j-(dn2-1)/2; n <= j+(dn2-1)/2; n++) dev += (intdb[n % DFT.N]-a)*(intdb[n % DFT.N]-a);
        dev = sqrt(dev/(float)dn2);


        sympeak = 0.0;
        for (n = 1; n <= dn3; n++) {
        sympeak += (sum_db[(j+n) % DFT.N]-globmin)*(sum_db[(j-n + DFT.N) % DFT.N]-globmin);
        }
        sympeak = sqrt(abs(sympeak)/(float)dn3);  // globmin > min

        sympeak2 = 0.0;
        for (n = 0; n <= (dn2-1)/2; n++) sympeak2 += (intdb[(j+n) % DFT.N]-globmin)*(intdb[(j-n + DFT.N) % DFT.N]-globmin);
        sympeak2 = sqrt(sympeak2/(2.0*dn2));

        peak[k % delay] = sympeak;


        if (1 || option_verbose) fprintf(fpout, " ; %10.4f ; %10.4f", a-globmin, dev);
        if (1 || option_verbose) fprintf(fpout, " ; %10.4f ; %10.4f", sympeak, sympeak2);

        mag = (sympeak - peak[(k+1)%delay])/ 3.0;  // threshold 3.0
        if ( mag < 0 ) mag = 0;
        if (1 || option_verbose) fprintf(fpout, " ; %d", mag);

        if (mag0 > 0 && mag == 0) fprintf(OUT, "peak: %+9.6f = %+9.1fHz\n", bin2fq(&DFT, max_db_idx), bin2freq(&DFT, max_db_idx));
        if (mag0 == 0 && mag > 0) {
            max_db_loc = sympeak;
            max_db_idx = j % DFT.N;
        }
        if (mag > 0 && sympeak > max_db_loc) {
            max_db_loc = sympeak;
            max_db_idx = j % DFT.N;
        }

        mag0 = mag;
        if (1 || option_verbose) fprintf(fpout, "\n");

        k++;
    }

    if (option_verbose) fprintf(stderr, "bin = %.2f Hz\n", bin2freq(&DFT, 1));

    if (bufavg) free(bufavg);
    if (bufpeak) free(bufpeak);

    end_dft(&DFT);
    fclose(fp);


    return 0;
}

