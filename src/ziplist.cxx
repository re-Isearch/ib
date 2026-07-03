#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>

static uint16_t read16(std::ifstream& fs)
{
    unsigned char b[2];
    fs.read((char*)b, 2);
    return (uint16_t)(b[0] | (b[1] << 8));
}

static uint32_t read32(std::ifstream& fs)
{
    unsigned char b[4];
    fs.read((char*)b, 4);
    return
        ((uint32_t)b[0]) |
        ((uint32_t)b[1] << 8) |
        ((uint32_t)b[2] << 16) |
        ((uint32_t)b[3] << 24);
}

bool listZipContents(const char* zipPath)
{
    std::ifstream fs(zipPath, std::ios::binary);

    if (!fs)
        return false;

    
    const char *name = zipPath;
    std::cout << "<?xml version=\"1.0\"?>\n\
<!DOCTYPE ARCHIVE SYSTEM \"archive.dtd\"> " << std::endl;
    std::cout << "<archive type=\"zip\">" << std::endl;
    std::cout << "<filename>" << name << "</filename>" << std::endl;
    std::cout << "<contents>" << std::endl;

    //
    // Find End Of Central Directory
    //
    fs.seekg(0, std::ios::end);

    std::streamoff fileSize = fs.tellg();

    const size_t searchSize =
        (fileSize < 65536) ? (size_t)fileSize : 65536;

    std::vector<char> buffer(searchSize);

    fs.seekg(fileSize - searchSize);
    fs.read(buffer.data(), searchSize);

    int eocdOffset = -1;

    for (int i = (int)searchSize - 22; i >= 0; --i)
    {
        if ((unsigned char)buffer[i + 0] == 0x50 &&
            (unsigned char)buffer[i + 1] == 0x4B &&
            (unsigned char)buffer[i + 2] == 0x05 &&
            (unsigned char)buffer[i + 3] == 0x06)
        {
            eocdOffset = i;
            break;
        }
    }

    if (eocdOffset < 0)
        return false;

    //
    // Parse EOCD
    //
    const unsigned char* eocd =
        (unsigned char*)&buffer[eocdOffset];

    uint16_t totalEntries =
        eocd[10] | (eocd[11] << 8);

    uint32_t centralDirOffset =
        eocd[16] |
        (eocd[17] << 8) |
        (eocd[18] << 16) |
        (eocd[19] << 24);

    //
    // Read central directory
    //
    fs.seekg(centralDirOffset);

    for (uint16_t i = 0; i < totalEntries; ++i)
    {
        uint32_t sig = read32(fs);

        // Central directory header signature
        if (sig != 0x02014b50)
            return false;

        fs.ignore(2); // version made by
        fs.ignore(2); // version needed
        // fs.ignore(2); // flags
	uint16_t flags = read16(fs);
        bool encrypted = (flags & 1) != 0;

        fs.ignore(2); // compression
        // fs.ignore(2); // mod time
        // fs.ignore(2); // mod date
	uint16_t modTime = read16(fs);
	uint16_t modDate = read16(fs);
        fs.ignore(4); // crc
        fs.ignore(4); // compressed size
        fs.ignore(4); // uncompressed size

        uint16_t fileNameLength = read16(fs);
        uint16_t extraLength = read16(fs);
        uint16_t commentLength = read16(fs);

        fs.ignore(2); // disk number
        fs.ignore(2); // internal attrs
        fs.ignore(4); // external attrs
        fs.ignore(4); // local header offset

        std::string name(fileNameLength, '\0');

        fs.read(&name[0], fileNameLength);

	char date[64];
	{
	  int seconds = (modTime & 0x1F) * 2;
	  int minutes = (modTime >> 5) & 0x3F;
	  int hours   = (modTime >> 11) & 0x1F;

	  int day     = modDate & 0x1F;
	  int month   = (modDate >> 5) & 0x0F;
	  int year    = ((modDate >> 9) & 0x7F) + 1980;

	  sprintf(date, "%04d-%02d-%02dT%02d:%02d:%02d", year, month, day, hours, minutes, seconds);
	}

        std::cout << "<item" << " date=\"" << date << "\"" 
		<< (encrypted?  " encrypted=1>" : ">" ) << name << "</item>" << std::endl;

        fs.ignore(extraLength + commentLength);
    }


    std::cout << "</contents></archive>" << std::endl;

    return true;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: ziplist file.zip\n";
        return 1;
    }

    if (!listZipContents(argv[1]))
    {
        std::cerr << "Invalid ZIP file\n";
        return 1;
    }

    return 0;
}
