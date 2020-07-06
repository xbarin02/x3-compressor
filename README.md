x3 &ndash; Experimental data compressor
=======================================

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

| Compressor |       lz4  |      gzip  |        xz  |      zstd  |    brotli  |        x3  |
| ---------- | ---------: | ---------: | ---------: | ---------: | ---------: | ---------: |
|    dickens |   2.2948   |   2.6461   |   3.6000   |   3.5765   |   3.6044   | **3.7168** |
|    mozilla |   2.3176   |   2.6966   | **3.8292** |   3.3769   |   3.6922   |   2.7432   |
|         mr |   2.3472   |   2.7138   |   3.6231   |   3.2132   |   3.5317   | **4.0364** |
|        nci |   9.1071   |  11.2311   |**23.1519** |  20.7925   |  22.0780   |  19.1103   |
|    ooffice |   1.7349   |   1.9907   | **2.5346** |   2.3587   |   2.4818   |   2.0668   |
|       osdb |   2.5290   |   2.7138   |   3.5456   |   3.2855   |   3.5812   | **3.6151** |
|    reymont |   3.1345   |   3.6396   |   5.0374   |   4.9060   |   4.9747   | **5.1010** |
|      samba |   3.5122   |   3.9950   | **5.7778** |   5.5267   |   5.7367   |   4.1871   |
|        sao |   1.2639   |   1.3613   | **1.6386** |   1.4479   |   1.5812   |   1.5042   |
|    webster |   2.9554   |   3.4372   |   4.9540   |   4.8970   |   4.9188   | **4.9685** |
|        xml |   6.9277   |   8.0709   |  12.2910   |  11.8004   |**12.4145** |   9.2249   |
|      x-ray |   1.1798   |   1.4035   |   1.8868   |   1.6457   |   1.8096   | **1.9649** |

The following options were used:

- `lz4 -9`
- `gzip --best`
- `xz -9 -e`
- `zstd --ultra -22`
- `brotli -q 11`

The files come from the [Silesia](http://sun.aei.polsl.pl/~sdeor/index.php?page=silesia) compression corpus.

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

Authors
-------

- David Barina, <ibarina@fit.vutbr.cz>

License
-------

This project is licensed under the MIT License.
See the [LICENSE.md](LICENSE.md) file for details.
