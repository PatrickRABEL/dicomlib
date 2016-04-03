# Introduction #

I haven't touched this library in a few years, but I'm getting back to it now.  So far I've just concentrated on getting it to compile nicely on a recent version of Ubuntu


# Dependencies #

You need the following packages.
```
$ apt-get install scons
$ apt-get install libboost-python-dev
$ apt-get install libboost-date-time-dev
```

You don't actually need boost-python for the main dicomlib C++ library, but you _do_ for pydicomlib.  Furthermore if you apt-get it then you'll be guaranteed to get all the boost dependencies that you **do** need.

# Building #

I use the http://www.scons.org build tool.  If you have all the right dependencies, building _should_ be as simple as:
```
$ cd _path_to_dicomlib_/dicomlib
$ scons
```

Which should result in **libdicom.a**, a static library which you can then use in your C++ dicom projects.

# Building pydicomlib #

```
$ cd path_to_dicomlib/pydicomlib
$ scons
```

This should result in `dicomlib.so`, which can then be used in python as follows:
```
$ python
>>> import dicom
```