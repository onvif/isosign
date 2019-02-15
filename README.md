# isosign - Library for signing MP4 files
Library provided by [ONVIF](https://www.onvif.org) for signing media files.

## Overview
This tiny library provides an easy way to sign video and audio files without modifying the media content. 
It is compatible with the ISO base file format better known as MP4 and the similar Quicktime file format.

The library consists of a single class box in namespace siso that supports the required manipulation mechanism.
A sample program is provided to demonstrate the usage of the library. It uses openssl as external command for calculating the signature in order to keep the sample application as simple as possible. 

The resulting files can be played back by any player that supports the original content. For signature verification you may use the Export File Player provided at www.github.com/onvif/oxfplayer.

## Signing

The openssl command line shows how to use openssl for creating a secure and correct digest. Notice that as detailed in the ONVIF Export File Format Specification the sample supports recursive signing by different entities to generate strong blockchains of evidence.

## License

BSD 3 clause

## References

 * [ISO/IEC 14496-12](https://www.iso.org/standard/68960.html)
 * [ONVIF Export File Format](https://www.onvif.org/specs/stream/ONVIF-ExportFileFormat-Spec.pdf)
