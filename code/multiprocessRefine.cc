#include <iostream>
#include <string>
#include <vector>
#include <string_view>
#include <charconv>
#include <cstdint>
#include <future>
#include <fstream>
#include <iomanip>
#include <unordered_map>
#include <filesystem>
#include <algorithm>

struct DataKey {
    std::string name;
    uint32_t date; //YYYYMMDD

    bool operator==(const DataKey& other) const { return date == other.date && name == other.name; }
};

struct DataKeyHasher {
    std::size_t operator()(const DataKey& k) const {
        std::size_t h1 = std::hash<std::string>{}(k.name);
        std::size_t h2 = std::hash<uint32_t>{}(k.date);
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2)); // Комбинируем хэши, так сделано в Boost (хз)
    }
};

struct DataValue {
    int id;
    std::vector<float> values;
};

using LocalMap = std::unordered_map<DataKey, DataValue, DataKeyHasher>;

//DD.MM.YYYY -> YYYYMMDD
inline uint32_t parseDateToNum(std::string_view dateStr) {
    if (dateStr.size() < 10) return 0;
    uint32_t day = 0;
    uint32_t month = 0;
    uint32_t year = 0;
    std::from_chars(dateStr.data(), dateStr.data() + 2, day);
    std::from_chars(dateStr.data() + 3, dateStr.data() + 5, month);
    std::from_chars(dateStr.data() + 6, dateStr.data() + 10, year);
    return (year * 10000) + (month * 100) + day;
}

inline std::string_view nextToken(const char*& curr, const char* end, char delim = ';') {
    const char* start = curr;
    while (curr < end && *curr != delim && *curr != '\n' && *curr != '\r') { curr++; }
    std::string_view token(start, curr - start);
    
    if (curr < end && (*curr == delim || *curr == '\r')) {
        curr++; 
        if (curr < end && *(curr - 1) == '\r' && *curr == '\n') curr++;
    }
    return token;
}

LocalMap processChunk(const std::filesystem::path& path, size_t startByte, size_t endByte, uint32_t minDate, uint32_t maxDate) {
    LocalMap localMap;

    std::ifstream file(path, std::ios::binary);
    if (!file) return localMap;

    size_t chunkSize = endByte - startByte;
    std::vector<char> buffer(chunkSize);
    
    file.seekg(startByte);
    file.read(buffer.data(), chunkSize);
    size_t bytesRead = file.gcount();

    const char* curr = buffer.data();
    const char* end = curr + bytesRead;

    while (curr < end) {
        std::string_view s_id = nextToken(curr, end, ';');
        if (s_id.empty()) continue;
        
        int rowId = 0;
        std::from_chars(s_id.data(), s_id.data() + s_id.size(), rowId);

        std::string_view s_name = nextToken(curr, end, ';');
        std::string_view s_date = nextToken(curr, end, ';');

        uint32_t rowDateNum = parseDateToNum(s_date);
        if (rowDateNum < minDate || rowDateNum > maxDate) {
            for (int i = 0; i < 7; ++i) nextToken(curr, end, (i == 6) ? '\n' : ';');
            continue;
        }

        std::vector<float> parsedValues;
        parsedValues.reserve(7);
        
        for (int i = 0; i < 7; ++i) {
            std::string_view s_val = nextToken(curr, end, (i == 6) ? '\n' : ';');
            float val = 0.0f;
            if (!s_val.empty()) std::from_chars(s_val.data(), s_val.data() + s_val.size(), val);
            parsedValues.push_back(val);
        }

        DataKey key{ std::string(s_name), rowDateNum };
        
        auto it = localMap.find(key);
        if (it != localMap.end()) {
            for (size_t i = 0; i < parsedValues.size(); ++i)
                if (i < it->second.values.size()) it->second.values[i] += parsedValues[i]; 
            
        } else localMap[std::move(key)] = DataValue{ rowId, std::move(parsedValues) };
    }

    return localMap;
}

LocalMap mergeTwoMaps(LocalMap&& map1, LocalMap&& map2) {
    if (map1.size() < map2.size()) std::swap(map1, map2);
    
    for (auto& [key, val2] : map2) {
        auto [it, inserted] = map1.try_emplace(key, std::move(val2));
        if (!inserted) {
            // Если ключ уже есть, суммируем значения float
            for (size_t i = 0; i < val2.values.size(); ++i) 
                if (i < it->second.values.size()) it->second.values[i] += val2.values[i];
            
        }
    }
    return std::move(map1);
}

// Рекурсивное попарное слияние вектора хэш-карт
LocalMap parallelReduce(std::vector<LocalMap>& maps) {
    if (maps.empty()) return {};
    
    while (maps.size() > 1) {
        std::vector<std::future<LocalMap>> futures;
        futures.reserve(maps.size() / 2);
        
        for (size_t i = 0; i < maps.size() - 1; i += 2) futures.push_back(std::async(std::launch::async, mergeTwoMaps, std::move(maps[i]), std::move(maps[i + 1])));
        
        std::vector<LocalMap> nextStageMaps;
        nextStageMaps.reserve(futures.size() + (maps.size() % 2));
        
        for (auto& f : futures) nextStageMaps.push_back(f.get());
        
        if (maps.size() % 2 != 0) nextStageMaps.push_back(std::move(maps.back())); // Если нечетное то на следующий этап
        
        maps = std::move(nextStageMaps);
    }
    
    return std::move(maps[0]);
}

struct ChunkInfo { 
    std::filesystem::path path; 
    size_t start;
    size_t end; 
};

inline void writeDateToStream(std::ofstream& out, uint32_t dateNum) {
    uint32_t year = dateNum / 10000;
    uint32_t month = (dateNum % 10000) / 100;
    uint32_t day = dateNum % 100;
    if (day < 10) out << '0';
    out << day << '.';
    if (month < 10) out << '0';
    out << month << '.' << year;
}

int main1(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <file1.csv> <file2.csv> ...\n";
        return 1;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    const uint32_t minDate = parseDateToNum("01.09.2026");
    const uint32_t maxDate = parseDateToNum("31.12.2026");

    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;

    std::vector<std::filesystem::path> files;
    for (int i = 1; i < argc; ++i) files.push_back(argv[i]); 
    
    std::vector<ChunkInfo> chunks;

    for (const auto& path : files) {
        if (!std::filesystem::exists(path) || std::filesystem::is_empty(path)) continue;

        size_t fileSize = std::filesystem::file_size(path);
        size_t idealChunkSize = fileSize / numThreads;
        
        std::ifstream file(path, std::ios::binary);
        size_t currentPos = 0;

        for (unsigned int i = 0; i < numThreads; ++i) {
            size_t startByte = currentPos;
            size_t targetEnd = currentPos + idealChunkSize;

            if (targetEnd >= fileSize || i == numThreads - 1) {
                chunks.push_back({path, startByte, fileSize});
                break;
            }

            file.seekg(targetEnd);
            std::string dummy;
            std::getline(file, dummy); // выравниваем по \n
            
            size_t endByte = file.tellg();
            currentPos = endByte;

            chunks.push_back({path, startByte, endByte});
        }
    }

    std::vector<std::future<LocalMap>> parseFutures;
    for (const auto& chunk : chunks)
        parseFutures.push_back(std::async(std::launch::async, processChunk, chunk.path, chunk.start, chunk.end, minDate, maxDate)); 

    std::vector<LocalMap> localMaps;
    localMaps.reserve(parseFutures.size());
    for (auto& f : parseFutures)
        localMaps.push_back(f.get());

    LocalMap finalMap = parallelReduce(localMaps);

    std::vector<std::pair<DataKey, DataValue>> sortedData(
        std::make_move_iterator(finalMap.begin()), 
        std::make_move_iterator(finalMap.end())
    );

    std::sort(sortedData.begin(), sortedData.end(), [](const auto& a, const auto& b) {
        if (a.first.name != b.first.name) return a.first.name < b.first.name;
        return a.first.date < b.first.date;
    });

    std::ofstream result("OutputData.csv", std::ios::binary);
    result << "id;name;date;value1;value2;value3;value4;value5;value6;value7\n";

    for (const auto& [key, val] : sortedData) {
        result << val.id << ';' << key.name << ';';
        writeDateToStream(result, key.date);
        result << ';';
        
        for (size_t j = 0; j < val.values.size(); ++j) {
            result << val.values[j];
            if (j < val.values.size() - 1) result << ';';
        }
        result << '\n';
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    std::cout << "Full processing finished in: " << elapsed << " ms\n";

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <file1.csv> <file2.csv> ...\n";
        return 1;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    const uint32_t minDate = parseDateToNum("01.09.2026");
    const uint32_t maxDate = parseDateToNum("31.12.2026");

    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;

    std::vector<std::filesystem::path> files;
    size_t totalBytes = 0;
    for (int i = 1; i < argc; ++i) {
        std::filesystem::path p(argv[i]);
        if (std::filesystem::exists(p) && !std::filesystem::is_empty(p)) {
            files.push_back(p);
            totalBytes += std::filesystem::file_size(p);
        }
    }

    if (files.empty()) return 0;

    size_t idealChunkSize = totalBytes / numThreads;
    if (idealChunkSize == 0) idealChunkSize = 4096;

    std::vector<ChunkInfo> chunks;
    size_t currentChunkAccumulated = 0;
    
    for (size_t fileIdx = 0; fileIdx < files.size(); ++fileIdx) {
        const auto& path = files[fileIdx];
        size_t fileSize = std::filesystem::file_size(path);
        size_t fileOffset = 0;
        
        std::ifstream file(path, std::ios::binary);

        while (fileOffset < fileSize) {
            size_t bytesLeftInFile = fileSize - fileOffset;
            size_t bytesNeededForCurrentChunk = idealChunkSize - currentChunkAccumulated;

            ChunkInfo chunk;
            chunk.path = path;
            chunk.start = fileOffset;

            if (bytesLeftInFile <= bytesNeededForCurrentChunk) {
                chunk.end = fileSize;
                fileOffset = fileSize;
                currentChunkAccumulated += bytesLeftInFile;
                
                if (currentChunkAccumulated >= idealChunkSize || fileIdx == files.size() - 1) {
                    currentChunkAccumulated = 0;
                }
                chunks.push_back(chunk);
            } 

            else {
                size_t targetEnd = fileOffset + bytesNeededForCurrentChunk;
                
                file.seekg(targetEnd);
                std::string dummy;
                std::getline(file, dummy);
                
                size_t alignedEnd = file.tellg();
                
                if (alignedEnd > fileSize || alignedEnd == std::string::npos) alignedEnd = fileSize;

                chunk.end = alignedEnd;
                fileOffset = alignedEnd;
                
                chunks.push_back(chunk);
                currentChunkAccumulated = 0;
            }
        }
    }

    std::vector<std::future<LocalMap>> parseFutures;
    for (const auto& chunk : chunks) {
        parseFutures.push_back(std::async(std::launch::async, processChunk, 
            chunk.path, chunk.start, chunk.end, minDate, maxDate)); 
    }

    std::vector<LocalMap> localMaps;
    localMaps.reserve(parseFutures.size());
    for (auto& f : parseFutures) {
        localMaps.push_back(f.get());
    }

    LocalMap finalMap = parallelReduce(localMaps);

    std::vector<std::pair<DataKey, DataValue>> sortedData(
        std::make_move_iterator(finalMap.begin()), 
        std::make_move_iterator(finalMap.end())
    );

    std::sort(sortedData.begin(), sortedData.end(), [](const auto& a, const auto& b) {
        if (a.first.name != b.first.name) return a.first.name < b.first.name;
        return a.first.date < b.first.date;
    });

    std::ofstream result("OutputData.csv", std::ios::binary);
    result << "id;name;date;value1;value2;value3;value4;value5;value6;value7\n";

    for (const auto& [key, val] : sortedData) {
        result << val.id << ';' << key.name << ';';
        writeDateToStream(result, key.date);
        result << ';';
        
        for (size_t j = 0; j < val.values.size(); ++j) {
            result << val.values[j];
            if (j < val.values.size() - 1) result << ';';
        }
        result << '\n';
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    std::cout << "Full processing finished in: " << elapsed << " ms\n";

    return 0;
}
