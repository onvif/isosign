/************************************************************************************
* Copyright (c) 2018 ONVIF.
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above copyright
*      notice, this list of conditions and the following disclaimer in the
*      documentation and/or other materials provided with the distribution.
*    * Neither the name of ONVIF nor the names of its contributors may be
*      used to endorse or promote products derived from this software
*      without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL ONVIF BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
************************************************************************************/
#ifdef _WIN32
#  define timegm _mkgmtime
#endif

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "siso.h"

using namespace siso;

uint64_t toFiletime(const char *time) {
	int y, m, d, h, M, s, mm = 0;
	if (sscanf(time, "%d-%d-%dT%d:%d:%d.%d", &y, &m, &d, &h, &M, &s, &mm) < 6) return 0;
	struct tm utc = { s, M, h, d, m - 1, y - 1900};
	uint64_t secs = timegm(&utc) + 11644473600;
	return secs * 10000000 + mm * 10000;
}

size_t readFile(const char *path, void *buffer, size_t size) {
	FILE *fdc = fopen(path, "rb");
	if (fdc == 0) { printf("Error opening file %s", path); exit(1); }
	size = fread(buffer, 1, size, fdc);
	fclose(fdc);
	return size;
}

int perr(const char *text) { perror(text); return -1; }

int main(int argc, char **argv)
{
	if (argc < 4) {
		printf("isosign <mp4 file> <cert file (der)> <cert key (pem)> [<yyyy-mm-ddThh:mm:ss.zzz>] [<comment>]\n");
		return 0;
	}
	const char *filePath = argv[1], *certPath = argv[2], *keyPath = argv[3];
	const char *startTime = argc >= 5 ? (isdigit(argv[4][0]) ? argv[4]: 0) : 0;
	const char *comment = argc >= 6 ? argv[5] : 0;

	//
	// Instantiate the file level box
	//
	box file(filePath);
	if (!file) return perr("Cannot open file");
	//
	// Lookup the file level meta box. If not existent append one.
	// Make sure that the box is the last in file in order to be able to extend it.
	//
	box meta = file['meta'];
	if (meta) {
		if (!meta.isLast()) return perr("Cannot modify meta box which is not last box in file");
	}
	else {
		meta = file.append('meta', 4);		// meta is a full box
	}
	//
	// Lookup the sumi box. If not existent create it. 
	// For simplicity reasons this example only stores the important file start time.
	//
	box sumi= meta.first('sumi', 4);		// meta is a full box
	if (!sumi) {
#pragma pack(push, 4)
		struct sumiData {
			uint8_t fragUuid[16], prevUuid[16], nextUuid[16];
			uint64_t startTime, duration;
			uint16_t prevUriSize, nextUriSize;
		} sumiData = {};
#pragma pack(pop)
		if (startTime) sumiData.startTime = box::swap64(toFiletime(startTime));
		meta.append('sumi', sizeof(sumiData), &sumiData);
	}
	//
	// Ensure that there is an ipro box.
	//
	box ipro = meta.first('ipro', 4);
	if (!ipro) ipro = meta.append('ipro', 6, "\0\0\0\0\0\x01");
	//
	// Every new signature appends a sinf box with scheme set to OEFF.
	//
	box sinf = ipro.append('sinf');
	sinf.append('schm', 12, "\0\0\0\0OEFF\0\0x01\0\0");
	box schi = sinf.append('schi');
	//
	// Create an all zero signature box to be filled later.
	// The sample code assumes 2048 bit key length resulting in 256 byte signature. 
	//
	box sibo = schi.append('sibo', 256);
	//
	// Read the ASN.1 encoded certificate and append it in the cert box.
	//
	uint8_t cert[2048];
	size_t certSize = readFile(certPath, cert, sizeof(cert));
	schi.append('cert', certSize, cert);
	//
	// For repeated signing a corrected start time may be stored.
	// The track ID of the first track is assumed to be Video. Depending on the box version the trackID offset is 5 for 64bit and 3 for 32 bit time field sizes.
	// Store the absolute start time as big endian 100ns since 1601.
	//
	if (sumi && startTime) {							// optionally write correction start time
		uint32_t tkhd[6];
		if (file['moov']['trak']['tkhd'].read(0, tkhd, sizeof(tkhd)) == sizeof(tkhd)) {
			struct { uint32_t entries;  uint32_t trackId; uint64_t startTime; 
			} cstb = { box::swap32(1), (tkhd[0] & 0xff) ? tkhd[5] : tkhd[3], box::swap64(toFiletime(startTime)) };
			schi.append('cstb', 16, &cstb);
		}
	}
	//
	// The operator may store an additional comment as a zero terminated string in the auib box.
	//
	if (comment) schi.append('auib', strlen(comment) + 1, comment);

	//
	// Now flush the file written so far and use openssl to calculate the signature.
	//
	file.flush();
	char cmd[10000];
	snprintf(cmd, sizeof(cmd), "openssl dgst -sha256 -sigopt rsa_padding_mode:pss -sigopt rsa_pss_saltlen:20 -sign %s -out signature.data %s", keyPath, filePath);
	if (system(cmd)) return perr("Error hashing file with openssl");
	//
	// Read the signature and update the sibo box created before with empty content.
	//
	uint8_t signature[1024];
	size_t sigSize = readFile("signature.data", signature, sizeof(signature));
	if (sibo.update(0, signature, sigSize)) return perr("Signature box size does not match");
	return 0;
}

