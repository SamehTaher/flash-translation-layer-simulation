/***************************************************************
 * A4Q2.c
 * Simulating a Flash Translation Layer (FTL) on an SSD
 *
 * SSD Model:
 *   - 512 physical blocks
 *   - Block size: 4 KB
 *   - Total size: 2 MB (2048 KB)
 *   - Backing file: SSD.txt
 *
 * Output:
 *   - Write count table
 *   - Dead block count
 *   - Min/Max/Avg write distribution
 *   - Comparison between first 256 and last 256 blocks
 *   - Runtime for 100 runs
 *
 ***************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>

#define NUM_BLOCKS      512
#define BLOCK_SIZE      4096
#define SSD_FILE        "SSD.txt"

#define NUM_LOGICAL     256
#define LIFESPAN        5

#define NUM_STRINGS     22
#define NUM_PER_STRING  10
#define TOTAL_WRITES    (NUM_STRINGS * NUM_PER_STRING)
#define BENCH_RUNS      100

/***************************************************************
 * Reference strings (22 x 10)
 ***************************************************************/
static const int reference_strings[NUM_STRINGS][NUM_PER_STRING] = {
    {  1,1,1,2,2,3,3,3,1,1 }, { 2,2,2,2,2,10,11,11,12,1 },
    {134,77,203,12,89,255,47,163,58,211 },
    {45,198,27,120,3,242,76,151,94,187 },
    {222,54,11,193,65,144,239,37,200,18 },

    {92,8,216,174,49,138,253,67,102,33 },
    {183,22,131,250,79,5,121,201,162,40 },
    {9,111,170,63,230,142,32,184,93,217 },
    {57,149,244,14,71,112,191,99,129,224 },
    {25,233,56,196,186,64,145,88,241,179 },

    {152,115,19,227,84,2,205,46,108,159 },
    {175,59,90,209,132,7,202,125,50,248 },
    {19,28,23,13,17,30,12,21,26,10 },
    {22,18,25,27,15,29,24,11,16,20 },
    {13,19,30,22,18,17,28,25,14,23 },

    {16,29,11,21,20,12,15,27,30,25 },
    {24,10,17,28,19,22,16,13,26,18 },
    {27,15,30,14,12,20,11,23,28,25 },
    {17,24,13,19,26,21,18,16,29,30 },
    {20,28,11,25,23,14,12,19,27,18 },

    {15,17,29,10,16,22,20,28,13,30 },
    {26,19,14,24,18,21,25,29,15,11 }
};

/***************************************************************
 * Initialize SSD file with zeros (simulate empty flash blocks)
 ***************************************************************/
void init_ssd_file(const char *filename) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("ERROR creating SSD.txt");
        exit(1);
    }

    char buf[BLOCK_SIZE];
    memset(buf, 0, sizeof(buf));

    for (int i = 0; i < NUM_BLOCKS; i++) {
        fwrite(buf, BLOCK_SIZE, 1, fp);
    }

    fclose(fp);
}

/***************************************************************
 * Wear-Leveling Selector:
 * Choose the block with the fewest writes and is not dead.
 ***************************************************************/
int select_physical_block(const int writes[], const int isDead[]) {
    int best = -1;
    int minW = INT_MAX;

    for (int i = 0; i < NUM_BLOCKS; i++) {
        if (!isDead[i] && writes[i] < minW) {
            minW = writes[i];
            best = i;
        }
    }

    return best;
}

/***************************************************************
 * Simulate the FTL for one full workload run.
 ***************************************************************/
void simulate_ftl(const char *ssdFile, int verbose) {
    int l2p[NUM_LOGICAL];
    int writes[NUM_BLOCKS];
    int isDead[NUM_BLOCKS];

    for (int i = 0; i < NUM_LOGICAL; i++) l2p[i] = -1;
    for (int i = 0; i < NUM_BLOCKS; i++) {
        writes[i] = 0;
        isDead[i] = 0;
    }

    FILE *fp = fopen(ssdFile, "r+b");
    if (!fp) {
        perror("ERROR opening SSD.txt");
        exit(1);
    }

    char buf[BLOCK_SIZE];
    memset(buf, 0xAB, sizeof(buf));  // dummy payload

    // Process logical writes from reference strings
    for (int s = 0; s < NUM_STRINGS; s++) {
        for (int j = 0; j < NUM_PER_STRING; j++) {

            int lba = reference_strings[s][j];
            if (lba < 0 || lba >= NUM_LOGICAL) continue;

            // Old physical block becomes stale (not erased)
            int oldP = l2p[lba];
            (void)oldP; // we don't reuse it, but conceptually invalidated

            // Wear-leveling selects new physical block
            int newP = select_physical_block(writes, isDead);
            if (newP == -1) {
                fprintf(stderr, "ERROR: No healthy block available.\n");
                fclose(fp);
                return;
            }

            // Update L2P table
            l2p[lba] = newP;

            // Simulate write to SSD.txt
            long offset = (long)newP * BLOCK_SIZE;
            fseek(fp, offset, SEEK_SET);
            fwrite(buf, BLOCK_SIZE, 1, fp);

            // Update wear count
            writes[newP]++;
            if (writes[newP] >= LIFESPAN)
                isDead[newP] = 1;
        }
    }

    fclose(fp);

    // Compute stats
    long totalWrites = 0;
    long firstHalfWrites = 0, secondHalfWrites = 0;
    int deadCount = 0;
    int minW = INT_MAX, maxW = 0;

    for (int i = 0; i < NUM_BLOCKS; i++) {
        totalWrites += writes[i];
        if (writes[i] < minW) minW = writes[i];
        if (writes[i] > maxW) maxW = writes[i];
        if (isDead[i]) deadCount++;

        if (i < NUM_LOGICAL) firstHalfWrites += writes[i];
        else secondHalfWrites += writes[i];
    }

    double avg = (double)totalWrites / NUM_BLOCKS;
    double avg1 = (double)firstHalfWrites / NUM_LOGICAL;
    double avg2 = (double)secondHalfWrites / NUM_LOGICAL;

    if (verbose) {
        printf("\n=== FTL Simulation Statistics ===\n\n");

        printf("Block writes:\n");
        for (int i = 0; i < NUM_BLOCKS; i++) {
            printf("%3d:%d  ", i, writes[i]);
            if ((i + 1) % 8 == 0) printf("\n");
        }

        printf("\nTotal logical writes : %d\n", TOTAL_WRITES);
        printf("Total physical writes: %ld\n", totalWrites);
        printf("Dead blocks          : %d\n", deadCount);
        printf("Write distribution   : min=%d  max=%d  avg=%.2f\n", minW, maxW, avg);
        printf("Avg first 256 blocks : %.2f\n", avg1);
        printf("Avg last 256 blocks  : %.2f\n\n", avg2);

        printf("Interpretation:\n");
        printf("- If avg1 ≈ avg2 and min/max are close,\n");
        printf("  the wear-leveling algorithm distributes writes evenly.\n");
    }
}

/***************************************************************
 * MAIN: Initialize SSD, run simulation, benchmark 100 runs
 ***************************************************************/
int main(void) {
    printf("Initializing SSD file...\n");
    init_ssd_file(SSD_FILE);

    printf("\n--- Single Simulation (with stats) ---\n");
    simulate_ftl(SSD_FILE, 1);

    printf("\n--- Benchmark: %d runs ---\n", BENCH_RUNS);
    clock_t start = clock();

    for (int i = 0; i < BENCH_RUNS; i++)
        simulate_ftl(SSD_FILE, 0);

    clock_t end = clock();
    double secs = (double)(end - start) / CLOCKS_PER_SEC;

    printf("Total time  : %.6f seconds\n", secs);
    printf("Avg per run : %.6f seconds\n", secs / BENCH_RUNS);

    return 0;
}

