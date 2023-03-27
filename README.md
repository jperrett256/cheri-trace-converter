# trace converter

This is a tool intended for converting traces.
Eventually, it may be for converting from my custom format to drcachesim format.
At the moment, it is simply being used to fix traces and (soon) add missing information by looking at past/future entries.

The custom format taken as input is generated from some code I have written on top of Mingle Chen's perfetto interceptor code, which rests on top of the perfetto backend of the tracing framework written by Alfredo Mazzinghi, created for [QEMU-CHERI](https://github.com/CTSRD-CHERI/qemu). If this seems a little bit more complicated than it needs to be, it is because it is. I may look into doing things slightly differently (which will both simplify things and potentially solve some issues I am having).

Running the following in a trace directory removes the drcachesim header/footer entries (mistakenly added):
```
/mnt/data/trace_converter/build/trace_converter trace.gz trace_fixed.gz $(cat trace_dbg.txt | sed -nE "s/^\tTotal: ([0-9]+)$/\1/p")
```
This also shows some useful statistics.

To only read the file and print statistics:
```
/mnt/data/trace_converter/build/trace_converter trace_fixed.gz
```
