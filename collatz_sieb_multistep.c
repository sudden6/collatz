#include <assert.h>
#include "stdio.h"
#include<stdlib.h>
#include<stdint.h>
#include <sys/time.h>
#include <time.h>


//Maximale Anzahl an Iterationen vor Abbruch (zur Vermeidung einer Endlosschleife)
#define max_nr_of_iterations 2000

typedef __uint128_t uint128_t;

// File-Handler für Ausgabedateien für betrachtete Reste (cleared) und
// Kandidatenzahlen (candidate)
FILE *f_cleared = NULL;
FILE *f_candidate;
// bzw. für Einlesen der zu bearbeitenden Reste (worktodo)
FILE *f_worktodo;

uint64_t checkpoint1 = 0;   // checks how many candidates survive after 3 multistep iterations
uint64_t checkpoint2 = 0;   // checks how many candidates survive after 6 multistep iterations
uint64_t checkpoint3 = 0;   // checks how often the multistep function is called

// globale Variablen für Start und Ende des Bereichs der zu bearbeitenden Reste
unsigned int idx_min;
unsigned int idx_max;

// vorberechnete Zweier- und Dreier-Potenzen
uint128_t pot3[64];

#define pot3_32Bit(x) ((uint32_t)(pot3[(x)]))
#define pot3_64Bit(x) ((uint64_t)(pot3[(x)]))

// Siebtiefe, bevor einzelne Startzahlen in den übrigbleibenden Restklassen
// erzuegt werden
#define SIEVE_DEPTH 58 // <=60
#define SIEVE_DEPTH_FIRST 32 // <=32
#define SIEVE_DEPTH_SECOND 40// <=40

#define MAX_PARALLEL_FACTOR 4   // wird für die speicher reservierungen benutzt

// Gesucht wird bis 87 * 2^60
#define SEARCH_LIMIT 87
//#define INNER_LOOP_OUTPUT

#define LOOP_END (SEARCH_LIMIT * (1 << (60 - SIEVE_DEPTH))) // Für Schleife im Siebausgang in sieve_third_stage

//#define INNER_LOOP_OUTPUT

#ifdef INNER_LOOP_OUTPUT
    // max_no_of_numbers = Anzahl der Zahlen in jeder Restklasse mod 9, die im Siebausgang in
    // sieve_third_stage auf einmal erzeugt und danach in first_multistep parallel ausgewertet werden
    #define MAX_NO_OF_NUMBERS ((LOOP_END+8)/9)
#else
    // max_no_of_numbers = Anzahl der Zahlen aller[!] Restklassen mod 9, die im Siebausgang in
    // sieve_third_stage auf einmal erzeugt und danach in first_multistep parallel ausgewertet werden
    #define MAX_NO_OF_NUMBERS ((LOOP_END+8)/9)*5
#endif

// maximale anzahl an datensätzen für die first_multistep_parallel methode
#define MS_PARALLEL_MAX_ITER (39/* *9*/ + MAX_PARALLEL_FACTOR - 1)

// Arrays zum Rausschreiben der Restklassen nach sieve_depth_first Iterationen
// reicht bis sieve_depth_first = 32;
// Das wären maximal 42 Millionen, wenn man corfactor = 1 setzt; für Vergleich mit etwa
// http://www.ams.org/journals/mcom/1999-68-225/S0025-5718-99-01031-5/S0025-5718-99-01031-5.pdf
// Seite 6/14 (Tabelle 1)

uint32_t * reste_array;
uint64_t * it32_rest;
uint32_t * it32_odd;
uint32_t * cleared_res;

// Anzahl übriger Restklassen nach sieve_depth_first Iterationen
uint64_t restcnt_it32;
// Anzahl in einer Restklasse gefundener Kandidaten
unsigned int no_found_candidates;

#define ms_depth 10 // 9 <= ms_depth <= 10

// Reste-Arrays für Multistep
uint32_t multistep_it_rest[1 << ms_depth];
uint32_t multistep_pot3_odd[1 << ms_depth];
float multistep_it_f[1 << ms_depth];
float multistep_it_maxf[1 << ms_depth];
float multistep_it_minf[1 << ms_depth];

// get the current wall clock time in seconds
double get_time() {
  struct timeval tp;
  gettimeofday(&tp, NULL);
  return tp.tv_sec + tp.tv_usec / 1000000.0;
}

// Füllt das 128-Bit-Dreier-Potenz-Array
void init_potarray()
{
    uint128_t p3 = 1;
    unsigned int i = 0;

    for ( ; i < 64; i++)
    {
        pot3[i] = p3;
        p3 *= 3;
    }
}

// Gibt Nummer der Restklasse einer Startzahl im
// betrachteten Intervall zurück; andernfalls -1
int nr_residue_class(const uint128_t start)
{
    uint_fast32_t startres32 = start;

    int i;
    for (i = 0; i < idx_max-idx_min; i++)
    {
        if (reste_array[i] == startres32)
            return (i + idx_min);
    }

    return -1;
}


// Bidschirm-Ausgabe int128 im Dezimalformat
// jeweils 6 Ziffern durch Kommata getrennt
void printf_128(uint128_t number)
{
    int digit[42];
    int cnt = 0;
    int loop;

    while (number>0)
    {
        digit[cnt]=number%10;
        number=number/10;
        cnt++;
    }

    for (loop=cnt-1;loop>=0;loop--)
    {
        printf("%i",digit[loop]);
        if ( (loop%6==0) && (loop>0) )
        printf(",");
    }
}


// Ausgabe int128 in Datei im Dezimalformat nach gonz
// mindigits: Minimale Stellenanzahl für rechtsbündige
// Ausrichtung.
# define mindigits_start  20
# define mindigits_record 39
void fprintf_128(uint128_t number, int mindigits)
{
    int digit[42];
    int cnt = 0;
    int loop;

    while (number>0)
    {
        digit[cnt]=number%10;
        number=number/10;
        cnt++;
    }

    for (loop=mindigits-1; loop >cnt-1; loop--)
    {
        fprintf(f_candidate," ");
    }

    for (loop=cnt-1;loop>=0;loop--)
    {
        fprintf(f_candidate,"%i",digit[loop]);
    }
}

//Berechnet Anzahl Binärstellen; übernommen von gonz
unsigned int bitnum(const uint128_t myvalue)
{
    int erg=1;
    uint128_t comp=2;
    while (myvalue>=comp)
    {
        erg++;
        comp=comp<<1;
    }
    return erg;
}

// Nachrechnen eines Rekords und Ausgabe; nach gonz
void print_candidate(const uint128_t start)
{
    //Anzahl gefundener Kandidaten um 1 hochzählen
    no_found_candidates++;

    uint128_t myrecord=0;
    uint128_t myvalue=start;
    unsigned int it = 0;

   while ((myvalue>=start) && (it < max_nr_of_iterations))
    {
        if (myvalue%2==1)
        {
            myvalue=3*myvalue+1;
            if (myvalue>myrecord)
                myrecord=myvalue;
        }
        else
            myvalue = myvalue >> 1;

        it++;
    }

    #pragma omp critical
    {
        if (it >= max_nr_of_iterations)
        {
            printf("*** Maximum Number of Iterations reached! ***\n");
            fprintf(f_candidate, "*** Maximum Number of Iterations reached! ***\n");
        }

        printf("** Start=");
        printf_128(start);
        printf(" %i Bit Record=",bitnum(start));
        printf_128(myrecord);
        printf(" %i Bit\n",bitnum(myrecord));

        fprintf_128(start, mindigits_start);
        fprintf(f_candidate," ");//%2i ",bitnum(start));
        fprintf_128(myrecord, mindigits_record);
        fprintf(f_candidate," %3i %8i\n",bitnum(myrecord), nr_residue_class(start));
        fflush(f_candidate);
    }
}


// Gibt an, um welchen Faktor ein Collatz-Faktor kleiner ist als die aktuell
// betrachtete Restklasse; Der Wert odd gibt dabei die Anzahl der
// bisher erfolgten (3x+1)-Schritte, und damit den Exponenten der
// Dreier-Potenz an, durch die das Modul der Restklasse teilbar ist
// laststepodd = 1 <==> Zahl entstand durch (3x+1)/2-Schritt. Dann muss
// der Fall Zahl == 2 (mod 3) nicht untersucht werden, da dies bei
// der Vorgängerzahl schon getan wurde.
double corfactor(const unsigned int odd, const uint64_t it_rest, const int laststepodd)
{
    const unsigned int rest = it_rest % 729;

    double minfactor = 1.0;
    double factor;

    if (odd >= 1)
    {
        if (!laststepodd && (rest % 3 == 2)) //2k+1 --> 3k+2
        {
            factor = 2.0 / 3.0 * corfactor(odd - 1, it_rest / 3 * 2 + 1, 0);

            if (factor < minfactor)
                minfactor = factor;
        }
    }
    else
        return minfactor;

    if (odd >= 2)
    {
        if (rest % 9 == 4) //8k+3 --> 9k+4
        {
            factor = 8.0 / 9.0 * corfactor(odd - 2, it_rest / 9 * 8 + 3, 0);

            if (factor < minfactor)
                minfactor = factor;
        }
    }
    else
        return minfactor;

    if (odd >= 4) //64k+7 --> 81k+10
    {
        if (rest % 81 == 10)
        {
            factor = 64.0 / 81.0 * corfactor(odd - 4, it_rest / 81 * 64 + 7, 0);

            if (factor < minfactor)
                minfactor = factor;
        }
    }
    else
        return minfactor;

    if (odd >= 5) //128k+95 --> 243k+182
    {
        if (rest % 243 == 182)
        {
            factor = 128.0 / 243.0 * corfactor(odd - 5, it_rest / 243 * 128 + 95, 0);

            if (factor < minfactor)
                minfactor = factor;
        }
    }
    else
        return minfactor;

    if (odd >= 6)
    {
        unsigned int p3 = 0;
        unsigned int rest2;

        switch (rest) // = "mod 729"
        {
            case  91: p3=6; rest2= 63; break; //512k+ 63 --> 729k+ 91
            case 410: p3=6; rest2=287; break; //512k+287 --> 729k+410
            case 433: p3=6; rest2=303; break; //512k+303 --> 729k+433
            case 524: p3=6; rest2=367; break; //512k+367 --> 729k+524
            case 587: p3=6; rest2=411; break; //512k+411 --> 729k+587
            case 604: p3=6; rest2=423; break; //512k+423 --> 729k+604
            case 661: p3=6; rest2=463; break; //512k+463 --> 729k+661
            case 695: p3=6; rest2=487; break; //512k+487 --> 729k+695
        }

        if (p3 == 6)
        {
            factor = 512.0 / 729.0
                    * corfactor(odd-6,it_rest/729 * 512 + rest2, 0);

            if (factor < minfactor)
                minfactor = factor;
        }
    }
    else
        return minfactor;

    return minfactor;
}

// Initialisiert die Arrays für die Multisteps
// Alle Restklassen mod 2^ms_depth werden durchgegangen, ihre Reste
// nach ms_depth Iterationen sowie auf diesem Weg das Maximum und
// Minimum (inkl. Betrachtung von möglichen Rückwärtsiterationen)
// berechnet und in den entsprechenden globalen Arrays gespeichert.
void init_multistep()
{
    unsigned int it;
    unsigned int odd;
    unsigned int it_rest;
    double min_f;
    double max_f;
    double it_f;
    double cormin;
    unsigned int nr_it_max;

    unsigned int rest;
    for (rest = 0; rest < (1 << ms_depth); rest++)
    {
        min_f = 1.0;
        max_f = 1.0;
        it_f  = 1.0;
        nr_it_max = 0;

        odd = 0;
        it_rest = rest;
        for (it = 1; it <= ms_depth; it++)
        {
            if (it_rest % 2 == 0)
            {
                it_rest = it_rest >> 1;
                it_f *= 0.5;
            }
            else
            {
                odd++;
                it_rest += (it_rest >> 1) + 1;
                it_f *= 1.5;
                if (it_f > max_f)
                {
                    max_f = it_f;
                    nr_it_max = it;
                }
            }
            cormin = it_f * corfactor(it_rest, odd, 0);
            if (cormin < min_f)
                min_f = cormin;
        }

        multistep_it_rest[rest] = it_rest;
        multistep_pot3_odd[rest] = pot3_32Bit(odd);
        multistep_it_f[rest] = it_f;
        multistep_it_maxf[rest] = max_f;
        multistep_it_minf[rest] = min_f;
    }
}

#define MS_MAX_CHECK_VAL    (1e16)
#define MS_MIN_CHECK_VAL    ((float)(0.98))
#define MS_DECIDE_VAL       (1e09)

// Erhält die Startzahl sowie das Ergebnis "number" ihrer nr_it-ten Iteration
// sowie eine Abschätzung des Quotienten it_f = number / start und berechnet
// via Zusamenfassung mehrerer Schritte die nächsten 6 * ms_depth (= 60) Schritte.
// Steigt it_f über 10^16, wird eine Ausgabe des Kandidaten mit dem in diesem
// Multistep (ms_depth = 10 Schritte) maximalen Wert erzeugt und danach abgebrochen.
// Diese Rechnungen erfolgen mit 64-Bit-Arithmetik.
// Wurde noch nicht die maximale Anzahl an Iterationen erreicht, muss der volle
// 128-Bit-Rest mit 128-Bit-Arithmetik nachgerechnet werden, um dann einen korrekten
// Rekursionsaufruf zu starten. Dies geschieht in zwei Etappen, in denen je 3 * ms_depth
// Iterationen zusammengefasst und gemeinsam berechnet werden.

unsigned int multistep(const uint128_t start, const uint128_t number,
                        const double it_f, const uint_fast32_t nr_it)
{
    checkpoint3++;
    uint64_t res = (uint64_t) number;
    double new_it_f = it_f;
    uint64_t res64 = res;
    uint8_t mark;

    // fest für 64/ms_depth = 6 implementiert!
    unsigned int small_res[6];

    // Die ersten 30 Iterationen: Wenn new_it_f < 5*10^10, dann kann kein neuer
    // Kandidat in diesen 30 Iterationen gefunden werden ==> keine Maximums-Prüfung
    // notwendig. Sonst kann in diesen Iterationen nicht der Startwert unterschritten
    // werden ==> keine Minimums-Prüfung notwendig.
    if (new_it_f < MS_DECIDE_VAL)
    {
        small_res[0] = res64 & ((1 << ms_depth) - 1);
        mark = new_it_f * multistep_it_minf[small_res[0]] <= MS_MIN_CHECK_VAL;

        res64 = (res64 >> ms_depth) * multistep_pot3_odd[small_res[0]]
                + multistep_it_rest[small_res[0]];
        new_it_f *= multistep_it_f[small_res[0]];

        small_res[1] = res64 & ((1 << ms_depth) - 1);
        mark |= new_it_f * multistep_it_minf[small_res[1]] <= MS_MIN_CHECK_VAL;

        res64 = (res64 >> ms_depth) * multistep_pot3_odd[small_res[1]]
                + multistep_it_rest[small_res[1]];
        new_it_f *= multistep_it_f[small_res[1]];

        small_res[2] = res64 & ((1 << ms_depth) - 1);
        mark |= new_it_f * multistep_it_minf[small_res[2]] <= MS_MIN_CHECK_VAL;

        if (mark) return 1;
        res64 = (res64 >> ms_depth) * multistep_pot3_odd[small_res[2]]
                + multistep_it_rest[small_res[2]];
        new_it_f *= multistep_it_f[small_res[2]];
    }
    else
    {
        small_res[0] = res64 & ((1 << ms_depth) - 1);
        mark = new_it_f * multistep_it_maxf[small_res[0]] > MS_MAX_CHECK_VAL;

        res64 = (res64 >> ms_depth) * multistep_pot3_odd[small_res[0]]
                + multistep_it_rest[small_res[0]];
        new_it_f *= multistep_it_f[small_res[0]];

        small_res[1] = res64 & ((1 << ms_depth) - 1);
        mark |= new_it_f * multistep_it_maxf[small_res[1]] > MS_MAX_CHECK_VAL;

        res64 = (res64 >> ms_depth) * multistep_pot3_odd[small_res[1]]
                + multistep_it_rest[small_res[1]];
        new_it_f *= multistep_it_f[small_res[1]];

        small_res[2] = res64 & ((1 << ms_depth) - 1);
        mark |= new_it_f * multistep_it_maxf[small_res[2]] > MS_MAX_CHECK_VAL;

        if (mark) // Kandidat gefunden, nun genaue Nachrechnung, daher hier
        {				  // keine Fortführung nötig
            print_candidate(start);
            return 1;
        }
        res64 = (res64 >> ms_depth) * multistep_pot3_odd[small_res[2]]
                + multistep_it_rest[small_res[2]];
        new_it_f *= multistep_it_f[small_res[2]];
    }

    // Nun die zweiten 30 Iterationen analog den ersten 30.
    if (new_it_f < 5e10)
    {
        small_res[3] = res64 & ((1 << ms_depth) - 1);
        mark = new_it_f * multistep_it_minf[small_res[3]] <= MS_MIN_CHECK_VAL;

        res64 = (res64 >> ms_depth) * multistep_pot3_odd[small_res[3]]
                + multistep_it_rest[small_res[3]];
        new_it_f *= multistep_it_f[small_res[3]];

        small_res[4] = res64 & ((1 << ms_depth) - 1);
        mark |= new_it_f * multistep_it_minf[small_res[4]] <= MS_MIN_CHECK_VAL;

        res64 = (res64 >> ms_depth) * multistep_pot3_odd[small_res[4]]
                + multistep_it_rest[small_res[4]];
        new_it_f *= multistep_it_f[small_res[4]];

        small_res[5] = res64 & ((1 << ms_depth) - 1);
        mark |= new_it_f * multistep_it_minf[small_res[5]] <= MS_MIN_CHECK_VAL;
        if (mark) return 1;
        new_it_f *= multistep_it_f[small_res[5]];
    }
    else
    {
        small_res[3] = res64 & ((1 << ms_depth) - 1);
        mark = new_it_f * multistep_it_maxf[small_res[3]] > MS_MAX_CHECK_VAL;

        res64 = (res64 >> ms_depth) * multistep_pot3_odd[small_res[3]]
                + multistep_it_rest[small_res[3]];
        new_it_f *= multistep_it_f[small_res[3]];

        small_res[4] = res64 & ((1 << ms_depth) - 1);
        mark |= new_it_f * multistep_it_maxf[small_res[4]] > MS_MAX_CHECK_VAL;

        res64 = (res64 >> ms_depth) * multistep_pot3_odd[small_res[4]]
                + multistep_it_rest[small_res[4]];
        new_it_f *= multistep_it_f[small_res[4]];

        small_res[5] = res64 & ((1 << ms_depth) - 1);
        mark |= new_it_f * multistep_it_maxf[small_res[5]] > MS_MAX_CHECK_VAL;
        if (mark) // Kandidat gefunden, nun genaue Nachrechnung, daher hier
        {				  // keine Fortführung nötig
            print_candidate(start);
            return 1;
        }
        new_it_f *= multistep_it_f[small_res[5]];
    }


    //Allgemein:
    //unsigned int small_res[32/ms_depth];
    //int i;
    //
    //for (i = 0; i < 32/ms_depth; i++ )
    //{
    //	small_res[i] = res32 & ((1 << ms_depth) - 1);
    //
    //	min_f = new_it_f * multistep_it_minf[small_res[i]];
    //	if (min_f <= 0.98) break;
    //
    //	res32 = (res32 >> ms_depth) * pot3_32Bit[multistep_odd[small_res[i]]]
    //	        + multistep_it_rest[small_res[i]];
    //	max_f = new_it_f * multistep_it_maxf[small_res[i]];
    //	new_it_f *= multistep_it_f[small_res[i]];
    //
    //	if (max_f > 1e15) found_candidate(start, max_f, nr_it + i*ms_depth + multistep_nr_it_max[small_res[i]]);
    //}



    //if (min_f > 0.98)
    {
        if (nr_it > max_nr_of_iterations)
        {
            print_candidate(start);
            return 1;
        }

        // Nun muss genau nachgerechnet werden: Dies geschieht in 2 Schritten, wo je 30
        // Iterationen zusammengefasst werden:

        // fest für 32/ms_depth = 3 implementiert!

        // Idee: a*2^3m + b*2^2m + c*2^m +small_res[0]
        //   --> a*3^p_0*2^2m + b*3^p_0*2^m + c*3^p_0 + it_rest[0]
        //     = a*3^p_0*2^2m + b*3^p_0*2^m + uebertrag[0]*2^m + small_res[1]
        //   --> a*3^p_0*3^p_1*2^m + b*3^p_0*3^p_1 + uebertrag[0]*3^p_1 + it_rest[1]
        //     = a*3^(p_0+p_1)*2^m + uebertrag[1]*2^m + small_res[2]
        //   --> a*3^(p_0+p_1+p_2) + uebertrag[1]*3^p_2 + it_rest[2];
        //
        // mit   uebertrag[0] = (c*3^p_0 + it_rest[0]) >> m
        // und   uebertrag[1] = ((b*3^p_0 + uebertrag[0])* 3^p_1 + it_rest[1]) >> m

        uint_fast32_t res32 = ((uint64_t) res) >> ms_depth;
        uint_fast32_t c = res32 & ((1 << ms_depth) - 1);
        res32 = res32 >> ms_depth;
        uint_fast32_t b = res32 & ((1 << ms_depth) - 1);

        uint_fast32_t uebertrag_0 = (c * multistep_pot3_odd[small_res[0]]
                                       + multistep_it_rest[small_res[0]]) >> ms_depth;

        uint_fast32_t uebertrag_1 = b * multistep_pot3_odd[small_res[0]]
                                       + uebertrag_0;

        uint64_t uebertrag = ((uint64_t) uebertrag_1
                                       * multistep_pot3_odd[small_res[1]]
                                       + multistep_it_rest[small_res[1]]) >> ms_depth;

        uebertrag *= multistep_pot3_odd[small_res[2]]; //uebertrag[1]*3^p_2
        uebertrag +=  multistep_it_rest[small_res[2]];        //uebertrag[1]*3^p_2 + it_rest[2]

        uint128_t int_nr = number >> (3 * ms_depth);  //a

        int_nr *=  multistep_pot3_odd[small_res[0]] 	  //a*3^(p_0+p_1+p_2)
                 + multistep_pot3_odd[small_res[1]]
                 + multistep_pot3_odd[small_res[2]];

        int_nr += uebertrag;



        res32 = ((uint64_t) int_nr) >> ms_depth;
        c = res32 & ((1 << ms_depth) - 1);
        res32 = res32 >> ms_depth;
        b = res32 & ((1 << ms_depth) - 1);

        uebertrag_0 = (c * multistep_pot3_odd[small_res[3]]
                       + multistep_it_rest[small_res[3]]) >> ms_depth;

        uebertrag_1 = b * multistep_pot3_odd[small_res[3]]
                      + uebertrag_0;

        uebertrag = ((uint64_t) uebertrag_1
                      * multistep_pot3_odd[small_res[4]]
                      + multistep_it_rest[small_res[4]]) >> ms_depth;

        uebertrag *= multistep_pot3_odd[small_res[5]]; //uebertrag[1]*3^p_2
        uebertrag +=  multistep_it_rest[small_res[5]];        //uebertrag[1]*3^p_2 + it_rest[2]

        uint128_t new_nr = int_nr >> (3 * ms_depth);  //a

        new_nr *= multistep_pot3_odd[small_res[3]] 	  //a*3^(p_0+p_1+p_2)
                + multistep_pot3_odd[small_res[4]]
                + multistep_pot3_odd[small_res[5]];

        new_nr += uebertrag;


        return (1 + multistep(start, new_nr, new_it_f, nr_it + 6 * ms_depth));

        //Allgemein wie folgt:
//		 unsigned __int128 new_nr = number;
//		for (i = 0; i < 64/ms_depth; i++ )
//		{
//			new_nr = (new_nr >> ms_depth) * pot3[multistep_odd[small_res[i]]]
//			         + multistep_it_rest[small_res[i]];
//		}
    }

}

unsigned int first_multistep(const uint128_t start, const uint128_t number,
                                const double it_f, const uint_fast32_t nr_it, uint64_t res64);

#define SMALL_RES_T uint32_t
#define POT3_ODD_T uint32_t
#define IT_REST_T uint32_t
#define IT_F_T float
#define IT_MINF_T float
#define MARK_T uint32_t
#define NEW_IT_F_T float

IT_REST_T it_rest_arr[MAX_PARALLEL_FACTOR]__attribute__ ((__aligned__(32)));
POT3_ODD_T pot3_odd_arr[MAX_PARALLEL_FACTOR]__attribute__ ((__aligned__(32)));
IT_F_T it_f_arr[MAX_PARALLEL_FACTOR]__attribute__ ((__aligned__(32)));
IT_MINF_T it_minf_arr[MAX_PARALLEL_FACTOR]__attribute__ ((__aligned__(32)));


uint64_t res64_arr[MAX_PARALLEL_FACTOR]__attribute__ ((__aligned__(32)));
NEW_IT_F_T new_it_f_arr[MAX_PARALLEL_FACTOR]__attribute__ ((__aligned__(32)));
MARK_T marks_arr[MAX_PARALLEL_FACTOR]__attribute__ ((__aligned__(32)));
SMALL_RES_T small_res_arr[MAX_PARALLEL_FACTOR]__attribute__ ((__aligned__(32)));

static inline void load_res64(uint128_t *restrict number, uint64_t *restrict res64)
{
    *res64 = (uint64_t) (*number);
}

static inline void update_small_res(uint64_t *restrict res64, SMALL_RES_T *restrict small_res)
{
    *small_res = (*res64) & ((1 << ms_depth) - 1);
}

static inline void fetch_ms_data(SMALL_RES_T *restrict small_res_p,
                   IT_REST_T *restrict it_rest, POT3_ODD_T *restrict pot3_odd,
                   IT_F_T *restrict it_f, IT_MINF_T *restrict it_minf)
{
    uint32_t small_res = *small_res_p;
    *it_f = multistep_it_f[small_res];
    *it_minf = multistep_it_minf[small_res];
    *it_rest = multistep_it_rest[small_res];
    *pot3_odd = multistep_pot3_odd[small_res];
}

static inline void update_res64(uint64_t *restrict res64, IT_REST_T *restrict it_rest, POT3_ODD_T *restrict pot3_odd)
{
    *res64 = ((*res64) >> ms_depth) * (*pot3_odd) + (*it_rest);
}

static inline void update_new_it_f(NEW_IT_F_T *restrict new_it_f, IT_F_T *restrict it_f)
{
    *new_it_f *= *it_f;
}

static inline void load_new_it_f(NEW_IT_F_T *restrict new_it_f, IT_F_T *restrict it_f, float g_it_f)
{
    *new_it_f = g_it_f * (*it_f);
}

static inline void load_mark_min(MARK_T *restrict mark,  float g_it_f, IT_MINF_T *restrict it_minf)
{
    *mark = ((float)g_it_f * (*it_minf) <= MS_MIN_CHECK_VAL) ? 0 : UINT32_MAX;
}

static inline void mark_min(MARK_T *restrict mark, NEW_IT_F_T *restrict new_it_f, IT_MINF_T *restrict it_minf)
{
    *mark &= ((*new_it_f) * (*it_minf) <= MS_MIN_CHECK_VAL) ? 0 : UINT32_MAX;
}

void ms_iter_first(uint128_t *restrict number, float it_f, uint_fast32_t cand_cnt)
{
    // prefetch 1
    for(uint_fast32_t ms_idx = 0; ms_idx < cand_cnt; ms_idx++)
    {
        load_res64(number + ms_idx,  &(res64_arr[ms_idx]));
        update_small_res(&(res64_arr[ms_idx]), &(small_res_arr[ms_idx]));
        fetch_ms_data(&(small_res_arr[ms_idx]), &(it_rest_arr[ms_idx]),
                      &(pot3_odd_arr[ms_idx]), &(it_f_arr[ms_idx]),
                      &(it_minf_arr[ms_idx]));
    }

    // compute 1
    for(uint_fast32_t ms_idx = 0; ms_idx < cand_cnt; ms_idx++)
    {
        load_mark_min(&(marks_arr[ms_idx]),it_f, &(it_minf_arr[ms_idx]));
        update_res64(&(res64_arr[ms_idx]), &(it_rest_arr[ms_idx]), &(pot3_odd_arr[ms_idx]));
        load_new_it_f(&(new_it_f_arr[ms_idx]), &(it_f_arr[ms_idx]), it_f);
        update_small_res(&(res64_arr[ms_idx]), &(small_res_arr[ms_idx]));
    }
}

void ms_iter_2_3(uint_fast32_t cand_cnt)
{
    // prefetch
    for(uint_fast32_t ms_idx = 0; ms_idx < cand_cnt; ms_idx++)
    {
        fetch_ms_data(&(small_res_arr[ms_idx]), &(it_rest_arr[ms_idx]), &(pot3_odd_arr[ms_idx]),
                      &(it_f_arr[ms_idx]), &(it_minf_arr[ms_idx]));
    }

    // compute
    for(uint_fast32_t ms_idx = 0; ms_idx < cand_cnt; ms_idx++)
    {
        mark_min(&(marks_arr[ms_idx]), &(new_it_f_arr[ms_idx]), &(it_minf_arr[ms_idx]));
        update_res64(&(res64_arr[ms_idx]), &(it_rest_arr[ms_idx]), &(pot3_odd_arr[ms_idx]));
        update_new_it_f(&(new_it_f_arr[ms_idx]), &(it_f_arr[ms_idx]));
        update_small_res(&(res64_arr[ms_idx]), &(small_res_arr[ms_idx]));
    }
}

//Erster Multistep ohne Maximums-Prüfung in den ersten 30 Iterationen; nach Amateur
unsigned int first_multistep_parallel(uint128_t*restrict start, uint128_t*restrict number,
                                const float it_f, const uint_fast32_t nr_it, uint_fast32_t cand_cnt)
{
    unsigned int credits = 1;

    // TODO: create struct for memory
/*
#define MULTISTEP0(GLOBAL_IDX) res64_arr[GLOBAL_IDX] = (uint64_t) (number[GLOBAL_IDX])
    // berechne small_res
#define MULTISTEP1(GLOBAL_IDX) small_res_arr[GLOBAL_IDX] = res64_arr[GLOBAL_IDX] & ((1 << ms_depth) - 1)
    // prefetch needed variables
#define MULTISTEP_PF(GLOBAL_IDX) multistep_it_minf_arr[GLOBAL_IDX] = multistep_it_minf[small_res_arr[GLOBAL_IDX]]; \
                                            multistep_it_rest_arr[GLOBAL_IDX] = multistep_it_rest[small_res_arr[GLOBAL_IDX]]; \
                                            multistep_it_f_arr[GLOBAL_IDX] = multistep_it_f[small_res_arr[GLOBAL_IDX]]; \
                                            next_pot3_arr[GLOBAL_IDX] = multistep_pot3_odd[small_res_arr[GLOBAL_IDX]]);

    // markiere Wertunterschreitungen
//#define MULTISTEP2(LOCAL_IDX, GLOBAL_IDX) mark_ ## LOCAL_IDX |= new_it_f_ ## LOCAL_IDX * multistep_it_minf[small_res_ ## LOCAL_IDX] <= MS_MIN_CHECK_VAL;
    // markiere Wertunterschreitungen, new_it_f_arr aus übergabeparameter, mark_min_arr als zuweisung
//#define MULTISTEP2A(LOCAL_IDX, GLOBAL_IDX) mark_ ## LOCAL_IDX = (it_f * multistep_it_minf[small_res_ ## LOCAL_IDX]) <= MS_MIN_CHECK_VAL;
    // berechne res64
#define MULTISTEP3(GLOBAL_IDX) res64_arr[GLOBAL_IDX]= (res64_arr[GLOBAL_IDX] >> ms_depth) \
                                                              * next_pot3_arr[GLOBAL_IDX] \
                                                              + multistep_it_rest_arr[GLOBAL_IDX]
    // berechne new_it_f
#define MULTISTEP4(GLOBAL_IDX) new_it_f_arr[GLOBAL_IDX] *= multistep_it_f[GLOBAL_IDX]
    // berechne new_it_f, it_f aus übergabeparameter
#define MULTISTEP4A(GLOBAL_IDX) new_it_f_arr[GLOBAL_IDX] = it_f * multistep_it_f_arr[GLOBAL_IDX]

    // SSE optimiert
#define MULTISTEP2A(LOCAL_IDX, GLOBAL_IDX) mark_arr[LOCAL_IDX][GLOBAL_IDX] = (it_f * multistep_it_minf_arr[GLOBAL_IDX]) <= MS_MIN_CHECK_VAL;
    // markiere Wertunterschreitungen
#define MULTISTEP2B(LOCAL_IDX, GLOBAL_IDX) mark_arr[LOCAL_IDX][GLOBAL_IDX] = new_it_f_arr[GLOBAL_IDX] * multistep_it_minf_arr[GLOBAL_IDX] <= MS_MIN_CHECK_VAL*/


    ms_iter_first(number, it_f, cand_cnt);

    ms_iter_2_3(cand_cnt);
    ms_iter_2_3(cand_cnt);


    for(uint_fast32_t ms_idx = 0; ms_idx < cand_cnt; ms_idx++)
    {
        if(marks_arr[ms_idx])
        {
            credits += first_multistep(start[ms_idx], number[ms_idx], new_it_f_arr[ms_idx], nr_it, res64_arr[ms_idx]);
        }
    }

    return credits;


}


//Erster Multistep ohne Maximums-Prüfung in den ersten 30 Iterationen; nach Amateur
unsigned int first_multistep(const uint128_t start, const uint128_t number,
                                const double it_f, const uint_fast32_t nr_it, uint64_t res64)
{
    double new_it_f = it_f;
    uint8_t mark;

    // fest für 64/ms_depth = 6 implementiert!

    uint64_t small_res[6];

    checkpoint1++;

    if (new_it_f < MS_DECIDE_VAL)
    {
        small_res[3] = res64 & ((1 << ms_depth) - 1);
        mark = new_it_f * multistep_it_minf[small_res[3]] <= MS_MIN_CHECK_VAL;

        res64 = (res64 >> ms_depth) * multistep_pot3_odd[small_res[3]]
                + multistep_it_rest[small_res[3]];
        new_it_f *= multistep_it_f[small_res[3]];

        small_res[4] = res64 & ((1 << ms_depth) - 1);
        mark |= new_it_f * multistep_it_minf[small_res[4]] <= MS_MIN_CHECK_VAL;

        res64 = (res64 >> ms_depth) * multistep_pot3_odd[small_res[4]]
                + multistep_it_rest[small_res[4]];
        new_it_f *= multistep_it_f[small_res[4]];

        small_res[5] = res64 & ((1 << ms_depth) - 1);
        mark |= new_it_f * multistep_it_minf[small_res[5]] <= MS_MIN_CHECK_VAL;
        if (mark) return 1;
        new_it_f *= multistep_it_f[small_res[5]];
    }
    else
    {
        small_res[3] = res64 & ((1 << ms_depth) - 1);
        mark = new_it_f * multistep_it_maxf[small_res[3]] > MS_MAX_CHECK_VAL;

        res64 = (res64 >> ms_depth) * multistep_pot3_odd[small_res[3]]
                + multistep_it_rest[small_res[3]];
        new_it_f *= multistep_it_f[small_res[3]];

        small_res[4] = res64 & ((1 << ms_depth) - 1);
        mark |= new_it_f * multistep_it_maxf[small_res[4]] > MS_MAX_CHECK_VAL;

        res64 = (res64 >> ms_depth) * multistep_pot3_odd[small_res[4]]
                + multistep_it_rest[small_res[4]];
        new_it_f *= multistep_it_f[small_res[4]];

        small_res[5] = res64 & ((1 << ms_depth) - 1);
        mark |= new_it_f * multistep_it_maxf[small_res[5]] > MS_MAX_CHECK_VAL;
        if (mark) // Kandidat gefunden, nun genaue Nachrechnung, daher hier
        {				  // keine Fortführung nötig
            print_candidate(start);
            return 1;
        }
        new_it_f *= multistep_it_f[small_res[5]];
    }

    checkpoint2++;

    // need 6 steps of recalculating, no checks needed, since we are already here

    uint64_t res = (uint64_t) number;
    uint64_t res64_local = res;

    for(int i = 0; i < 6; i++)
    {
        small_res[i] = res64_local & ((1 << ms_depth) - 1);
        res64_local = (res64_local >> ms_depth)
                      * multistep_pot3_odd[small_res[i]]
                      + multistep_it_rest[small_res[i]];
    }


    //if (min_f > 0.98)
    {
        uint_fast32_t res32 = ((uint64_t) res) >> ms_depth;
        uint_fast32_t c = res32 & ((1 << ms_depth) - 1);
        res32 = res32 >> ms_depth;
        uint_fast32_t b = res32 & ((1 << ms_depth) - 1);

        uint_fast32_t uebertrag_0 = (c * multistep_pot3_odd[small_res[0]]
                                       + multistep_it_rest[small_res[0]]) >> ms_depth;

        uint_fast32_t uebertrag_1 = b * multistep_pot3_odd[small_res[0]]
                                       + uebertrag_0;

        uint64_t uebertrag = ((uint64_t) uebertrag_1
                                       * multistep_pot3_odd[small_res[1]]
                                       + multistep_it_rest[small_res[1]]) >> ms_depth;

        uebertrag *= multistep_pot3_odd[small_res[2]]; //uebertrag[1]*3^p_2
        uebertrag +=  multistep_it_rest[small_res[2]];        //uebertrag[1]*3^p_2 + it_rest[2]

        uint128_t int_nr = number >> (3 * ms_depth);  //a

        int_nr *= multistep_pot3_odd[small_res[0]] 	  //a*3^(p_0+p_1+p_2)
                + multistep_pot3_odd[small_res[1]]
                + multistep_pot3_odd[small_res[2]];

        int_nr += uebertrag;



        res32 = ((uint64_t) int_nr) >> ms_depth;
        c = res32 & ((1 << ms_depth) - 1);
        res32 = res32 >> ms_depth;
        b = res32 & ((1 << ms_depth) - 1);

        uebertrag_0 = (c * multistep_pot3_odd[small_res[3]])
                       + multistep_it_rest[small_res[3]] >> ms_depth;

        uebertrag_1 = b * multistep_pot3_odd[small_res[3]]
                      + uebertrag_0;

        uebertrag = ((uint64_t) uebertrag_1
                      * multistep_pot3_odd[small_res[4]])
                      + multistep_it_rest[small_res[4]] >> ms_depth;

        uebertrag *= multistep_pot3_odd[small_res[5]]; //uebertrag[1]*3^p_2
        uebertrag +=  multistep_it_rest[small_res[5]];        //uebertrag[1]*3^p_2 + it_rest[2]

        uint128_t new_nr = int_nr >> (3 * ms_depth);  //a

        new_nr *= multistep_pot3_odd[small_res[3]] 	  //a*3^(p_0+p_1+p_2)
                + multistep_pot3_odd[small_res[4]]
                + multistep_pot3_odd[small_res[5]];

        new_nr += uebertrag;


        return (1 + multistep(start, new_nr, new_it_f, nr_it + 6 * ms_depth));
    }

}

// Siebt Restklassen bis Iteration sieve_depth_first (32) vor und speichert die
// Ergebnisse der übrigbleibenden Restklassen in den folgenden globalen Arrays:
// k * 2^sieve_depth_first + reste_array[i] --> k * 3^it32_odd[i] + it32_rest[i]
// Dabei wird der Zähler restcnt_it32 bei jeder neuen hier gefundenen Restklasse,
// die noch zu betrachten ist, um Eins hochgezählt, sodass nach dem Ende der init-
// Methode dieser Wert die Anzahl aller dieser Restklassen mod 2^sieve_depth_first
// angibt.
void sieve_first_stage (const int nr_it, const uint_fast32_t rest,
                        const uint64_t it_rest,
                        const double it_f, const uint_fast32_t odd)
{
    if (nr_it >= SIEVE_DEPTH_FIRST)
    {
        // Nur Daten für Restklassen herausschreiben, die gerade betrachtet werden
        if ((idx_min <= restcnt_it32) && (restcnt_it32 < idx_max))
        {
            reste_array[restcnt_it32 - idx_min] = rest;
            it32_rest[restcnt_it32 - idx_min] = it_rest;
            it32_odd[restcnt_it32 - idx_min] = odd;
        }
        restcnt_it32++;
    }
    else
    {
        //new_rest = 0 * 2^nr_it + rest
        uint_fast32_t new_rest = rest;
        uint64_t new_it_rest = it_rest;
        double new_it_f = it_f;
        uint_fast32_t new_odd = odd;
        int laststepodd = 0;

        if ((new_it_rest & 1) == 0)
        {
            new_it_rest = new_it_rest >> 1;
            new_it_f *= 0.5;
        }
        else
        {
            new_it_rest += (new_it_rest >> 1) + 1;
            new_it_f *= 1.5;
            new_odd++;
            laststepodd = 1;
        }

        if (new_it_f * corfactor(new_odd, new_it_rest, laststepodd) > 0.98)
            sieve_first_stage(nr_it + 1, new_rest, new_it_rest, new_it_f, new_odd);

        //new_rest = 1 * 2^nr_it + rest
        new_rest = rest + ((uint32_t)1 << nr_it);//pot2_32Bit[nr_it];
        new_it_rest = it_rest + pot3_64Bit(odd);
        new_it_f = it_f;
        new_odd = odd;
        laststepodd = 0;

        if ((new_it_rest & 1) == 0)
        {
            new_it_rest = new_it_rest >> 1;
            new_it_f *= 0.5;
        }
        else
        {
            new_it_rest += (new_it_rest >> 1) + 1;
            new_it_f *= 1.5;
            new_odd++;
            laststepodd = 1;
        }

        if (new_it_f * corfactor(new_odd, new_it_rest, laststepodd) > 0.98)
            sieve_first_stage(nr_it + 1, new_rest, new_it_rest, new_it_f, new_odd);
    }
}

const uint_fast8_t testmod9[90] = {
                          1, 1, 0, 1, 0, 0, 1, 1, 0, // Um Verzweigungen nach startmod9 >=9 zu vermeiden
                          1, 1, 0, 1, 0, 0, 1, 1, 0,
                          1, 1, 0, 1, 0, 0, 1, 1, 0,
                          1, 1, 0, 1, 0, 0, 1, 1, 0,
                          1, 1, 0, 1, 0, 0, 1, 1, 0,
                          1, 1, 0, 1, 0, 0, 1, 1, 0,
                          1, 1, 0, 1, 0, 0, 1, 1, 0,
                          1, 1, 0, 1, 0, 0, 1, 1, 0,
                          1, 1, 0, 1, 0, 0, 1, 1, 0,
                          1, 1, 0, 1, 0, 0, 1, 1, 0};

const int pot2mod9 = (1 << (SIEVE_DEPTH % 6)) % 9;
const uint128_t pot2_sieve_depth = (((uint128_t) 1) << 31) << (SIEVE_DEPTH - 31);
const uint128_t nine_times_pot2_sieve_depth =
                                           (((uint128_t) 9) << 31) << (SIEVE_DEPTH - 31);

// Siebt Reste von Iteration sieve_depth_second bis sieve_depth (40 bis 58)
// Für die übrigbleibenen Restklassen werden alle Zahlen bis 87*2^60 erzeugt und
// , wenn sie nicht kongruent 2 (mod 3) oder 4 (mod 9) sind, zur weiteren
// Berechnung der Multistep-Methode übergeben.

uint128_t start_arr[MAX_NO_OF_NUMBERS]__attribute__ ((__aligned__(32)));
uint128_t it_arr[MAX_NO_OF_NUMBERS]__attribute__ ((__aligned__(32)));

unsigned int sieve_third_stage (const uint64_t nr_it, const uint64_t rest,
                                const uint128_t it_rest,
                                const double it_f, const uint64_t odd)
{
    // Zählt, wie oft in den Multisteps die teuren 128-Bit-Nachrechnungen durchgeführt werden
    unsigned int credits = 0;

    if (nr_it >= SIEVE_DEPTH)
    {
        // Siebausgang
        // k * 2^sieve_depth + rest --> k * 3^odd + it_rest
        // sinngemäß:
        // for (k = 0; k < 2^(67-sieve_depth); k++)
        //   {Teste Startzahl k * 2^sieve_depth + rest}

        uint128_t start_0 = rest;
        uint128_t it_0 = it_rest;

        uint128_t start;
        uint128_t it;

        uint_fast32_t ms_start_count = 0;

        uint_fast32_t startmod9 = rest % 9;

        int j;	//Umgruppierung der Reihenfolge nach Amateur
        int k;

        for (j = 0; j < 9; j++)
        {
            if (testmod9[startmod9])
            {
                start = start_0;
                it    = it_0;
#ifdef INNER_LOOP_OUTPUT
                ms_start_count = 0;
#endif

                for (k=0; 9 * k + j < 87 * (1 << (60 - SIEVE_DEPTH)); k++)
                {
                    start_arr[ms_start_count] = start;
                    it_arr[ms_start_count] = it;
                    ms_start_count++;
                    // start = rest + (k * 9 + j) * 2^sieve_depth
                    start += nine_times_pot2_sieve_depth;
                    it    += pot3[odd+2]; // = " ... + 9*pot3[odd]
                }
#ifdef INNER_LOOP_OUTPUT
                first_multistep_parallel(start_arr, it_arr, it_f, SIEVE_DEPTH, ms_start_count);
#endif
            }

            start_0 += pot2_sieve_depth;
            it_0    += pot3[odd];
            startmod9 += pot2mod9; // startmod9 <= 8 + 9 * 8 < 90
        }
#ifndef INNER_LOOP_OUTPUT
        for(uint64_t i = 0; i < ms_start_count; i += MAX_PARALLEL_FACTOR)
        {
            uint_fast32_t count = ms_start_count - i >= MAX_PARALLEL_FACTOR ? MAX_PARALLEL_FACTOR : ms_start_count - i;
            first_multistep_parallel(&(start_arr[i]), &(it_arr[i]), it_f, SIEVE_DEPTH, count);
        }
#endif
    }
    else
    {
        //new_rest = 0 * 2^nr_it + rest
        uint64_t  new_rest = rest;
        uint128_t new_it_rest = it_rest;
        double new_it_f = it_f;
        unsigned int new_odd = odd;
        int laststepodd = (new_it_rest & 1);

        if ( laststepodd == 0)
        {
            new_it_rest = new_it_rest >> 1;
            new_it_f *= 0.5;
        }
        else
        {
            new_it_rest += (new_it_rest >> 1) + 1;
            new_it_f *= 1.5;
            new_odd++;
        }


        if ((new_it_f > 10) || //nachfolgende Bedingung benötigt sieve_depth <= 60
            (new_it_f * corfactor(new_odd, (uint64_t) new_it_rest, laststepodd) > 0.98))
        {
            credits += sieve_third_stage(nr_it + 1, new_rest, new_it_rest, new_it_f, new_odd);
        }

        //new_rest = 1 * 2^nr_it + rest
        new_rest = rest + (((uint64_t)1) << nr_it); //pot2[nr_it];
        new_it_rest = it_rest + pot3[odd];
        new_it_f = it_f;
        new_odd = odd;
        laststepodd = (new_it_rest & 1);

        if (laststepodd == 0)
        {
            new_it_rest = new_it_rest >> 1;
            new_it_f *= 0.5;
        }
        else
        {
            new_it_rest += (new_it_rest >> 1) + 1;
            new_it_f *= 1.5;
            new_odd++;
        }

        if ((new_it_f > 10) || //nachfolgende Bedingung benötigt sieve_depth <= 60
            (new_it_f * corfactor(new_odd, (uint64_t) new_it_rest, laststepodd) > 0.98))
        {
            credits += sieve_third_stage(nr_it + 1, new_rest, new_it_rest, new_it_f, new_odd);
        }
    }

    return credits;
}

// Siebt Reste von Iteration sieve_depth_first bis Iteration sieve_depth_second (32 bis 40)
uint64_t sieve_second_stage (const int nr_it, const uint64_t rest,
                                     const uint64_t it_rest,
                                     const double it_f, const uint_fast32_t odd)
{
    uint64_t credits = 0;

    if (nr_it >= SIEVE_DEPTH_SECOND)
    {
        credits += sieve_third_stage(nr_it, rest, it_rest, it_f, odd);
    }
    else
    {
        //new_rest = 0 * 2^nr_it + rest
        uint64_t new_rest = rest;
        uint64_t new_it_rest = it_rest;
        double new_it_f = it_f;
        unsigned int new_odd = odd;
        int laststepodd = 0;

        if ((new_it_rest & 1) == 0)
        {
            new_it_rest = new_it_rest >> 1;
            new_it_f *= 0.5;
        }
        else
        {
            new_it_rest += (new_it_rest >> 1) + 1;
            new_it_f *= 1.5;
            new_odd++;
            laststepodd = 1;
        }

        if (new_it_f * corfactor(new_odd, new_it_rest, laststepodd) > 0.98)
        {
            credits += sieve_second_stage(nr_it + 1, new_rest, new_it_rest,
                                            new_it_f, new_odd);
        }

        //new_rest = 1 * 2^nr_it + rest
        new_rest = rest + (((uint64_t)1) << nr_it); //pot2[nr_it];
        new_it_rest = it_rest + pot3_64Bit(odd);
        new_it_f = it_f;
        new_odd = odd;
        laststepodd = 0;

        if ((new_it_rest & 1) == 0)
        {
            new_it_rest = new_it_rest >> 1;
            new_it_f *= 0.5;
        }
        else
        {
            new_it_rest += (new_it_rest >> 1) + 1;
            new_it_f *= 1.5;
            new_odd++;
            laststepodd = 1;
        }

        if (new_it_f * corfactor(new_odd, new_it_rest, laststepodd) > 0.98)
        {
            credits += sieve_second_stage(nr_it + 1, new_rest, new_it_rest,
                                            new_it_f, new_odd);
        }
    }

    return credits;
}

// Liest schon abgearbeitete Restklassen aus
// Gibt 0 zurück, wenn noch kein File "cleared.txt" existierte und also bisher noch
// keine Restklasse abgearbeitet wurde.
int resume()
{
    char string1[20];
    char string2[20];
    char string3[40];
    char string4[20];

    uint_fast32_t i;
    uint_fast32_t rest;
    uint64_t credits;
    uint_fast32_t no_of_cand;

    //f_cleared = fopen("cleared.txt","r");
    if (f_cleared != NULL)
    {
        int dummy;
        // Tabellenkopf einlesen
        dummy = fscanf(f_cleared, "%s %s %s %s\n", string1, string2,
                                                   string3, string4);

        while ( fscanf(f_cleared, "%u %u %llu %u\n", &i, &rest, &credits,
                                                     &no_of_cand) >= 2)
        {
            if ((idx_min <= i) && (i < idx_max))
            cleared_res[i-idx_min] = 1;
        }

        fclose(f_cleared);

        return 1;
    }
    else
    {
        return 0;
    }
}

//Führt Initialisierung des Siebs aus
void init()
{
    int cleared_file_exists = resume();

    f_cleared = fopen("cleared.txt","a");
    if (!cleared_file_exists)
    {
        fprintf(f_cleared, "No_ResCl ResCl_mod_2^32 Multistep_Calls #Cand\n");
        fflush(f_cleared);
    }

    f_candidate =fopen("candidates.txt", "r");
    int candidate_file_exists = (f_candidate != NULL);
    if (candidate_file_exists) fclose(f_candidate);

    f_candidate =fopen("candidates.txt", "a");
    if (!candidate_file_exists)
    {
        fprintf(f_candidate, "               Start                                  Record");
        fprintf(f_candidate, " Bit No_ResCl\n");
        fflush(f_candidate);
    }

    // Bisher 0 betrachtete Restklassen mod 2^32
    restcnt_it32 = 0;

    // noch zu betrachtende Restklassen mod 2^sieve_depth_first erzeugen:

    // 2^1 * k + 1 --> 3^1 * k + 2
    sieve_first_stage(1, 1, 2, 1.5, 1);

    // Mehr Restklassen als restcnt_it32 gibt es nicht --> idx_max nach oben dadurch abschneiden
    if (idx_max > restcnt_it32)
    {
        idx_max = restcnt_it32;

        // ggegebenenfalls muss dann auch idx_min angepasst werden
        if (idx_min > idx_max) idx_min = idx_max;
    }

    printf("\nSieve initialized\n");

}


// Liest zu bearbeitenden Restklassen-Bereich ein
// gibt 0 zurück bei Einlese-Fehler, sonst 1
int worktodo()
{
    idx_min = 0;
    idx_max = 0;

    if (fscanf(f_worktodo, "%u %u ", &idx_min, &idx_max) != 2) return 0;

    //if (idx_max > restcnt_it32) idx_max = restcnt_it32;
    if (idx_min > idx_max) idx_min = idx_max;

    return 1;
}


int main()
{
    // Initialisierungen
    init_potarray();
    init_multistep();

    // Start und Ende des zu bearbeitendenen Bereichs aus Datei auslesen.

    f_worktodo = fopen("worktodo.txt","r");
    if (f_worktodo == NULL)
    {
        printf("\n File 'worktodo.txt' is missing! \n\n");
        printf("Every line in this file consists of two numbers:\n");
        printf("<No. of first Residue Class> <No. of last+1 Residue Class>\n\n");

        printf("press enter to exit.\n");
        getchar();

        return 1;
    }

    while (worktodo()) // Solang zeilenweise je eine Arbeitsaufgabe eingelesen werden kann
    {
        uint_fast32_t i;
        unsigned int rescnt = 0;

        // Anzahl in diesem Durchlauf zu untersuchender Restklassen
        unsigned int size = idx_max - idx_min;

        //Speicher-Allokation für Ausgabe nach erstem Siebschritt
        reste_array = malloc(size * sizeof(uint32_t));
        it32_rest   = malloc(size * sizeof(uint64_t));
        it32_odd    = malloc(size * sizeof(uint32_t));
        cleared_res = calloc(size,  sizeof(uint32_t));

        if ((reste_array == NULL) || (it32_rest == NULL) || (it32_odd == NULL) || (cleared_res == NULL))
        {
            printf("Error while allocating memory!\n\n");

            printf("press enter to exit.\n");
            getchar();

            return 1;
        }


        // Initialisierung des Siebs
        init();

        uint64_t credits;

        printf("Test of Residue Classes No. %d -- %d\n\n",idx_min, idx_max);
        double start_time = get_time();

        // Möglichkeit zur Parallelisierung
        #pragma omp parallel for private(i, credits, no_found_candidates) shared(rescnt) schedule(dynamic)
        for (i = 0; i < idx_max - idx_min; i++)
        {
            if (!cleared_res[i])
            { // Nur, wenn Rest noch nicht abgearbeitet
                no_found_candidates = 0;
                credits = sieve_second_stage(SIEVE_DEPTH_FIRST, reste_array[i], it32_rest[i],
                                             ((double) pot3_64Bit(it32_odd[i])) / (((uint64_t)1) << SIEVE_DEPTH_FIRST),
                                             it32_odd[i]);

                #pragma omp critical
                {
                    rescnt++;
                    printf("%4u: Residue Class No. %8u is done. %fs\n", rescnt, i + idx_min, get_time() - start_time);
                    fprintf(f_cleared, "%8u     %10u %15llu %5u\n", i+idx_min, reste_array[i], credits,
                                                                    no_found_candidates);
                    fflush(f_cleared);
                }
            }
        }

        //Speicherfreigabe nach getaner Arbeit
        free(reste_array);
        free(it32_rest);
        free(it32_odd);
        free(cleared_res);

        // Ausgabedateien, die durch init() geöffnet wurden, wieder schließen
        if (f_candidate != NULL) fclose(f_candidate);
        if (f_cleared   != NULL) fclose(f_cleared);
    }

    // Nun auch Eingabedatei "worktodo.txt" wieder schließen
    if (f_worktodo != NULL) fclose(f_worktodo);

    // Keine Aufgaben mehr in Datei vorhanden
    //int remove_failed = remove("worktodo.txt");
    //if (remove_failed) printf("Could not delete file 'worktodo.txt'.\n\n");

    printf("chk1: %lu chk2: %lu chk3: %lu\n", checkpoint1, checkpoint2, checkpoint3);
    //printf("press enter to exit.\n");
    //getchar();

    return 0;
}
