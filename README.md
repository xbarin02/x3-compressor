x3 &ndash; Golomb-Rice data compressor
======================================

[![Build Status](https://travis-ci.org/xbarin02/x3-compressor.svg?branch=master)](https://travis-ci.org/xbarin02/x3-compressor)

What is it?
-----------

Experimental data compressor based on unary and Golomb-Rice coding.

The algorithm
-------------

The **x3** uses a dictionary and encodes various code-stream events using a unary and adaptive Golomb-Rice coding.

Benchmarks
----------

| Compressor |       lz4  |      gzip  |     bzip2  |        xz  |      zstd  |    brotli  |            x3 |
| ---------- | ---------: | ---------: | ---------: | ---------: | ---------: | ---------: | ------------: |
|    dickens |   2.2948   |   2.6461   | **3.6407** |   3.6000   |   3.5765   |   3.6044   |      3.3513   |
|     enwik6 |   2.4393   |   2.8140   |   3.5546   |   3.4395   |   3.3313   | **3.5585** |      2.8251   |
|         mr |   2.3472   |   2.7138   | **4.0841** |   3.6231   |   3.2132   |   3.5317   |      3.6318   |
|    ooffice |   1.7349   |   1.9907   |   2.1492   | **2.5346** |   2.3587   |   2.4818   |      1.9460   |
|       osdb |   2.5290   |   2.7138   | **3.5984** |   3.5456   |   3.2855   |   3.5812   |      2.1760   |
| pointcloud |   1.8708   |   2.4190   |   2.8344   |   3.0019   |   3.1359   | **3.1894** |      2.5329   |
|    reymont |   3.1345   |   3.6396   | **5.3178** |   5.0374   |   4.9060   |   4.9747   |      4.6132   |
|        sao |   1.2639   |   1.3613   |   1.4678   | **1.6386** |   1.4479   |   1.5812   |      1.3453   |
|        xml |   6.9277   |   8.0709   |  12.1157   |  12.2910   |  11.8004   |**12.4145** |      7.5800   |
|      x-ray |   1.1798   |   1.4035   | **2.0918** |   1.8868   |   1.6457   |   1.8096   |      1.6030   |

The following options were used:

- `lz4 -9`
- `gzip --best`
- `bzip2 -9`
- `xz -9 -e`
- `zstd --ultra -22`
- `brotli -q 11`

Authors
-------

- David Barina, <ibarina@fit.vutbr.cz>

License
-------

This project is licensed under the MIT License.
See the [LICENSE.md](LICENSE.md) file for details.
