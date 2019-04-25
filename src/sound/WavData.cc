#include "WavData.hh"
#include "MSXException.hh"
#include "endian.hh"

namespace openmsx {

template<typename T>
static const T* read(span<uint8_t> raw, size_t offset, size_t count = 1)
{
	if ((offset + count * sizeof(T)) > raw.size()) {
		throw MSXException("Read beyond end of wav file.");
	}
	return reinterpret_cast<const T*>(raw.data() + offset);
}

WavData::WavData(File file)
{
	// Read and check header
	auto raw = file.mmap();
	struct WavHeader {
		char riffID[4];
		Endian::L32 riffSize;
		char riffType[4];
		char fmtID[4];
		Endian::L32 fmtSize;
		Endian::L16 wFormatTag;
		Endian::L16 wChannels;
		Endian::L32 dwSamplesPerSec;
		Endian::L32 dwAvgBytesPerSec;
		Endian::L16 wBlockAlign;
		Endian::L16 wBitsPerSample;
	};
	const auto* header = read<WavHeader>(raw, 0);
	if (memcmp(header->riffID, "RIFF", 4) ||
	    memcmp(header->riffType, "WAVE", 4) ||
	    memcmp(header->fmtID, "fmt ", 4)) {
		throw MSXException("Invalid WAV file.");
	}
	unsigned bits = header->wBitsPerSample;
	if ((header->wFormatTag != 1) || ((bits != 8) && (bits != 16))) {
		throw MSXException("WAV format unsupported, must be 8 or 16 bit PCM.");
	}
	freq = header->dwSamplesPerSec;
	unsigned channels = header->wChannels;

	// Skip any extra format bytes
	size_t pos = 20 + header->fmtSize;

	// Find 'data' chunk
	struct DataHeader {
		char dataID[4];
		Endian::L32 chunkSize;
	};
	const DataHeader* dataHeader;
	while (true) {
		// Read chunk header
		dataHeader = read<DataHeader>(raw, pos);
		pos += sizeof(DataHeader);
		if (!memcmp(dataHeader->dataID, "data", 4)) break;
		// Skip non-data chunk
		pos += dataHeader->chunkSize;
	}

	// Read and convert sample data
	length = dataHeader->chunkSize / ((bits / 8) * channels);
	buffer.resize(length);
	auto convertLoop = [&](const auto* in, auto convertFunc) {
		for (unsigned i = 0; i < length; ++i) {
			buffer[i] = convertFunc(*in);
			in += channels; // discard all but the first channel
		}
	};
	if (bits == 8) {
		convertLoop(read<uint8_t>(raw, pos, length * channels),
		            [](uint8_t u8) { return (int16_t(u8) - 0x80) << 8; });
	} else {
		convertLoop(read<Endian::L16>(raw, pos, length * channels),
		            [](Endian::L16 s16) { return int16_t(s16); });
	}
}

} // namespace openmsx
