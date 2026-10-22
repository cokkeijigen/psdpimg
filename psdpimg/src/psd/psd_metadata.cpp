#include "psd/psd_metadata.hpp"

namespace psdpimg::psd
{
	namespace
	{
		constexpr uint8_t resolution_bytes[16]
		{
			0x00, 0x48, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x00, 0x48, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03,
		};

		constexpr uint8_t version_bytes[45]
		{
			0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x07, 0x00, 0x70, 0x00, 0x73, 0x00, 0x64, 0x00,
			0x70, 0x00, 0x69, 0x00, 0x6D, 0x00, 0x67, 0x00, 0x00, 0x00, 0x07, 0x00, 0x70, 0x00, 0x73, 0x00,
			0x64, 0x00, 0x70, 0x00, 0x69, 0x00, 0x6D, 0x00, 0x67, 0x00, 0x00, 0x00, 0x01,
		};

		constexpr char xmp_bytes[]
		{
			"<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\x0A"
			"<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\x0A"
			"  <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\x0A"
			"    <rdf:Description rdf:about=\"\"\x0A"
			"        xmlns:dc=\"http://purl.org/dc/elements/1.1/\"\x0A"
			"        xmlns:photoshop=\"http://ns.adobe.com/photoshop/1.0/\"\x0A"
			"        xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\"\x0A"
			"      dc:format=\"application/vnd.adobe.photoshop\"\x0A"
			"      photoshop:ColorMode=\"3\"\x0A"
			"      xmp:CreateDate=\"2018-10-24T00:00:00+08:00\"\x0A"
			"      xmp:MetadataDate=\"2018-10-24T11:45:14+08:00\"\x0A"
			"      xmp:ModifyDate=\"2018-10-24T11:45:14+08:00\"/>\x0A"
			"  </rdf:RDF>\x0A"
			"</x:xmpmeta>\x0A"
			"<?xpacket end=\"w\"?>"
		};
	}

	auto resolution_info() noexcept -> std::span<const uint8_t>
	{
		return resolution_bytes;
	}

	auto version_info() noexcept -> std::span<const uint8_t>
	{
		return version_bytes;
	}

	auto xmp_packet() noexcept -> std::string_view
	{
		return std::string_view{ xmp_bytes, sizeof(xmp_bytes) - 1 };
	}

	auto xmp_name() noexcept -> std::string_view
	{
		return "pimg";
	}
}
