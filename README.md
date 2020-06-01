x3 &ndash; Golomb-Rice data compressor
======================================

[![Build Status](https://travis-ci.org/xbarin02/x3-compressor.svg?branch=master)](https://travis-ci.org/xbarin02/x3-compressor)

What is it?
-----------

Experimental data compressor based on arithmetic coding.

The algorithm
-------------

The **x3** uses a dictionary and encodes various code-stream events using an arithmetic coding.
Details are given [here](ALGORITHM.md).

Benchmarks
----------

| Compressor |       lz4  |      gzip  |     bzip2  |        xz  |      zstd  |    brotli  |        x3  |
| ---------- | ---------: | ---------: | ---------: | ---------: | ---------: | ---------: | ---------: |
|    dickens |   2.2948   |   2.6461   | **3.6407** |   3.6000   |   3.5765   |   3.6044   |   3.5618   |
|     enwik8 |   2.3653   |   2.7438   |   3.4472   | **4.0271** |   3.9462   |   3.8847   |   3.7369   |
|         mr |   2.3472   |   2.7138   | **4.0841** |   3.6231   |   3.2132   |   3.5317   |   4.0053   |
|    ooffice |   1.7349   |   1.9907   |   2.1492   | **2.5346** |   2.3587   |   2.4818   |   2.0633   |
|       osdb |   2.5290   |   2.7138   | **3.5984** |   3.5456   |   3.2855   |   3.5812   |   2.8647   |
| pointcloud |   1.8708   |   2.4190   |   2.8344   |   3.0019   |   3.1359   | **3.1894** |   2.7923   |
|    reymont |   3.1345   |   3.6396   | **5.3178** |   5.0374   |   4.9060   |   4.9747   |   4.8891   |
|        sao |   1.2639   |   1.3613   |   1.4678   | **1.6386** |   1.4479   |   1.5812   |   1.3903   |
|        xml |   6.9277   |   8.0709   |  12.1157   |  12.2910   |  11.8004   |**12.4145** |   8.8361   |
|      x-ray |   1.1798   |   1.4035   | **2.0918** |   1.8868   |   1.6457   |   1.8096   |   1.8544   |

The following options were used:

- `lz4 -9`
- `gzip --best`
- `bzip2 -9`
- `xz -9 -e`
- `zstd --ultra -22`
- `brotli -q 11`

Most files come from the [Silesia](http://sun.aei.polsl.pl/~sdeor/index.php?page=silesia) compression corpus. The enwik8 comes from the [Hutter Prize](http://prize.hutter1.net/) page.

Usage
-----

```
./x3 [arguments] [input-file] [output-file]
```

Arguments :

- `-d`     : force decompression
- `-z`     : force compression
- `-f`     : overwrite existing output file
- `-k`     : keep (don't delete) input file (default)
- `-t NUM` : maximum number of matches (affects compression ratio and speed)
- `-w NUM` : window size (in kilobytes, affects compression ratio and speed)
- `-T NUM` : spawns NUM compression threads

Authors
-------

- David Barina, <ibarina@fit.vutbr.cz>

License
-------

This project is licensed under the MIT License.
See the [LICENSE.md](LICENSE.md) file for details.
