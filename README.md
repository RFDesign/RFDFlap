# RFD FLAP

## CLI:

```
FLAP CLI tool
-------------------------------------------------------------------------------

USAGE
-------------------------------------------------------------------------------
flap_cli [options] <files and folders>

-------------------------------------------------------------------------------
BIN and FLI files are flights. FLV files are run on every flight.

--out:<dir>         Where the results go. They go next to the
                    flight when this is not given.

--fli_out:<dir>     Where the FLI of a BIN goes. It goes next to
                    the BIN when this is not given.

--recursive         Folders inside the given folders are read.

--force             All the work is done again.

--dry_run           Lists what was found and stops.

--thread_count:<n>  The number of worker threads.

--no_compress       The FLI files are written with no compression.

--import:<dir>      A folder that FLV imports are looked for in.

--graph_width:<n>   The width of a graph image, in pixels.

--graph_height:<n>  The height of a graph image, in pixels.

--graph_csv         A graph is written as CSV and not as PNG.

--graph_grid        A graph image has a line at each tick.

--graph_font:<file> The font of the text of a graph image. It
                    is the font of the app when this is not given.

--graph_modes       A graph image has the flight modes as a
                    tint of its plot.

--graph_flags       A graph image has the flags of its program
                    as lanes at the bottom of its plot.
```


## FLI:

```
FLI Converter

-------------------------------------------------------------------------------

USAGE EXAMPLES
-------------------------------------------------------------------------------
fli flight.bin
Converts `flight.bin` to FLI format.

fli flight.bin --out:flight.fli
Converts `flight.bin` to FLI, and stores it in `flight.fli`.
-------------------------------------------------------------------------------
DESCRIPTION

This utility converts ArduPilot DataFlash logs to the Flight Log Index format (.fli).
Supported input formats: BIN.
-------------------------------------------------------------------------------
ARGUMENTS

--fli               Specifies that the utility should convert input files
                    to the Flight Log Index format (.fli).

--out:<path>        Specifies the path to which output data should be written. If
                    not specified, the utility will choose a fallback based on
                    input file names.

--no_compress       Disables compression of the output FLI file.

--thread_count:<n>  Specifies the number of worker threads to use.
```
