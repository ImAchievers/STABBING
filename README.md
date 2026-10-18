# STABBING: Search Tool, All Biome-Based, Including Numerous Generosities

Measures the biome touching 0, 0 in 1:4 cells and Blocks^2 for every seed in order and writes each one to a text file.

## Build

Settings to adjust in the #defines before compiling:

    START_SEED    Starting seed for a new run
    THREADS       Worker threads, adjust for your PC
    LB            1 for Large Biomes
    Y_LEVEL       Y-level to sample at, 256 for the surface
    TARGETS       Biomes to measure, set to -1 to search all of them. Must be lowercase biome names separated by commas and an underscore if multiple words (desert, eroded_badlands)
    EXCLUDE       Biomes to skip, -1 for none. Same format as TARGETS (river, deep_ocean)
    MIN_SIZE      Minimum size in 1:4 cells to write to the output file, keep at 0 to write everything

MSYS2/MinGW on Windows, or Linux:

    make

## Usage

    ./STABBING.exe                  start from the checkpoint, or the START_SEED #define if there isn't one
    ./STABBING.exe <seed>           start from this seed instead
    ./STABBING.exe single <seed>    measure one seed and print it like measure.exe

## Output

Every seed goes to stabbing_results.txt, one per line. LB, other Y-levels, and runs targeting specific biomes, get their own results and checkpoint
files (stabbing_LB_results.txt, stabbing_y0_results.txt, stabbing_targets_results.txt...) so different results won't be confused.

With the TARGETS or EXCLUDE defines set, seeds whose biome at 0, 0 isn't searched are skipped without measuring. Unfortunately the way I needed to do this is a bit confusing, but a biome in both lists is skipped.

So for example, if you only wanted to search for coast biomes set the new defines to:

    #define TARGETS beach, snowy_beach, stony_shore
    #define EXCLUDE -1

or if you want a Best: in the status line that isn't a desert (lmao) set them to this:

    #define TARGETS -1
    #define EXCLUDE desert

or if you want to find a specific sized biome like a 200k+ 1:4 Warm Ocean, set them to:

    #define TARGETS warm_ocean
    #define EXCLUDE -1
    #define MIN_SIZE 200000

With MIN_SIZE set, any measurements below that threshold aren't written to the results file, but still count for the status line's Best.

Lines are written in seed order no matter which thread finishes first. stabbing_checkpoint.txt is saved every 30 seconds and on Ctrl+C,
so stopping with Ctrl+C never skips/repeats a seed.

The status line every 20 seconds shows seeds/s, the seed it's on, and the biggest measurement so far.

## Example Output in the results file:
    Seed | Biome | 1:4 Size | Blocks^2 Size
    0 River 7672 122752
    1 Deep_Ocean 41046 656736
    2 Forest 72589 1161424