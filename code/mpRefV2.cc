#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <string_view>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <future>
#include <thread>
#include <cstdint>
#include <utility>
#include <filesystem>

struct ChunkInfo {
    std::filesystem::path path;
    size_t start;
    size_t end;
};

struct Data {
    int id;
    std::string_view name; 
    uint32_t date;
    std::vector<float> values;
};

struct ThreadResult {
    std::vector<std::vector<char>> buffers; 
    std::vector<Data> dataList;
};

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

inline void writeDateToStream(std::ofstream& out, uint32_t dateNum) {
    uint32_t year = dateNum / 10000;
    uint32_t month = (dateNum % 10000) / 100;
    uint32_t day = dateNum % 100;

    if (day < 10) out << '0';
    out << day << '.';
    
    if (month < 10) out << '0';
    out << month << '.' << year;
}

inline std::string_view nextToken(const char*& curr, const char* end, char delim = ';') {
    const char* start = curr;
    while (curr < end && *curr != delim && *curr != '\n' && *curr != '\r') {
        curr++;
    }
    std::string_view token(start, curr - start);
    
    if (curr < end && (*curr == delim || *curr == '\r')) {
        curr++; 
        if (curr < end && *(curr - 1) == '\r' && *curr == '\n') {
            curr++; // Пропускаем Windows-перенос строки \r\n
        }
    }
    return token;
}

ThreadResult processChunk(const std::filesystem::path& path, size_t startByte, size_t endByte, uint32_t minDate, uint32_t maxDate) {
    ThreadResult result;
    
    std::ifstream file(path, std::ios::binary);
    if (!file) return result;

    size_t chunkSize = endByte - startByte;
    
    std::vector<char> rawBuffer(chunkSize);
    file.seekg(startByte);
    file.read(rawBuffer.data(), chunkSize);
    size_t bytesRead = file.gcount();
    rawBuffer.resize(bytesRead);

    const char* curr = rawBuffer.data();
    const char* end = curr + bytesRead;

    result.dataList.reserve(bytesRead / 60); 

    while (curr < end) {
        std::string_view s_id = nextToken(curr, end, ';');
        if (s_id.empty()) continue;
        
        int rowId = 0;
        std::from_chars(s_id.data(), s_id.data() + s_id.size(), rowId);

        std::string_view s_name = nextToken(curr, end, ';');
        std::string_view s_date = nextToken(curr, end, ';');

        uint32_t rowDateNum = parseDateToNum(s_date);
        
        if (rowDateNum < minDate || rowDateNum > maxDate) {
            for (int i = 0; i < 7; ++i) {
                nextToken(curr, end, (i == 6) ? '\n' : ';');
            }
            continue;
        }

        std::vector<float> parsedValues;
        parsedValues.reserve(7);
        for (int i = 0; i < 7; ++i) {
            std::string_view s_val = nextToken(curr, end, (i == 6) ? '\n' : ';');
            float val = 0.0f;
            if (!s_val.empty()) {
                std::from_chars(s_val.data(), s_val.data() + s_val.size(), val);
            }
            parsedValues.push_back(val);
        }

        result.dataList.push_back(Data{ rowId, s_name, rowDateNum, std::move(parsedValues) });
    }

    result.buffers.push_back(std::move(rawBuffer));

    std::sort(result.dataList.begin(), result.dataList.end(), [](const Data& a, const Data& b) {
        if (a.name != b.name) return a.name < b.name;
        return a.date < b.date;
    });

    if (!result.dataList.empty()) {
        std::vector<Data> compressed;
        compressed.reserve(result.dataList.size());
        
        compressed.push_back(std::move(result.dataList[0]));
        
        for (size_t i = 1; i < result.dataList.size(); ++i) {
            auto& last = compressed.back();
            auto& current = result.dataList[i];
            
            if (last.name == current.name && last.date == current.date) {
                // Если имя и дата совпали — суммируем значения float
                for (size_t j = 0; j < current.values.size(); ++j) {
                    if (j < last.values.size()) last.values[j] += current.values[j];
                }
            } else {
                compressed.push_back(std::move(current));
            }
        }
        result.dataList = std::move(compressed);
    }

    return result;
}

ThreadResult mergeTwoResults(ThreadResult&& r1, ThreadResult&& r2) {
    ThreadResult out;
    out.dataList.reserve(r1.dataList.size() + r2.dataList.size());
    
    out.buffers = std::move(r1.buffers);
    out.buffers.insert(out.buffers.end(), 
        std::make_move_iterator(r2.buffers.begin()), 
            std::make_move_iterator(r2.buffers.end()));

    size_t i = 0, j = 0;
    while (i < r1.dataList.size() && j < r2.dataList.size()) {
        auto& item1 = r1.dataList[i];
        auto& item2 = r2.dataList[j];

        if (item1.name == item2.name && item1.date == item2.date) {
            for (size_t k = 0; k < item2.values.size(); ++k) {
                if (k < item1.values.size()) item1.values[k] += item2.values[k];
            }
            out.dataList.push_back(std::move(item1));
            i++; j++;
        } 
        else if (item1.name < item2.name || (item1.name == item2.name && item1.date < item2.date)) {
            out.dataList.push_back(std::move(item1));
            i++;
        } 
        else {
            out.dataList.push_back(std::move(item2));
            j++;
        }
    }

    while (i < r1.dataList.size()) out.dataList.push_back(std::move(r1.dataList[i++]));
    while (j < r2.dataList.size()) out.dataList.push_back(std::move(r2.dataList[j++]));

    return out;
}

ThreadResult parallelReduce(std::vector<ThreadResult>& results) {
    if (results.empty()) return {};
    
    while (results.size() > 1) {
        std::vector<std::future<ThreadResult>> futures;
        futures.reserve(results.size() / 2);
        
        for (size_t i = 0; i < results.size() - 1; i += 2) {
            futures.push_back(std::async(std::launch::async, mergeTwoResults, 
                std::move(results[i]), std::move(results[i + 1])));
        }
        
        std::vector<ThreadResult> nextStage;
        nextStage.reserve(futures.size() + (results.size() % 2));
        
        for (auto& f : futures) {
            nextStage.push_back(f.get());
        }
        
        if (results.size() % 2 != 0) {
            nextStage.push_back(std::move(results.back()));
        }
        
        results = std::move(nextStage);
    }
    
    return std::move(results[0]);
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
                if (alignedEnd > fileSize || alignedEnd == std::string::npos) {
                    alignedEnd = fileSize;
                }

                chunk.end = alignedEnd;
                fileOffset = alignedEnd;
                
                chunks.push_back(chunk);
                currentChunkAccumulated = 0; 
            }
        }
    }

    std::vector<std::future<ThreadResult>> parseFutures;
    for (const auto& chunk : chunks) {
        parseFutures.push_back(std::async(std::launch::async, processChunk, 
            chunk.path, chunk.start, chunk.end, minDate, maxDate)); 
    }

    std::vector<ThreadResult> localResults;
    localResults.reserve(parseFutures.size());
    for (auto& f : parseFutures) {
        localResults.push_back(f.get());
    }

    ThreadResult finalResult = parallelReduce(localResults);

    std::ofstream result("OutputData.csv", std::ios::binary);
    result << "id;name;date;value1;value2;value3;value4;value5;value6;value7\n";

    for (const auto& row : finalResult.dataList) {
        result << row.id << ';' << row.name << ';'; 
        writeDateToStream(result, row.date);
        result << ';';
        
        for (size_t j = 0; j < row.values.size(); ++j) {
            result << row.values[j];
            if (j < row.values.size() - 1) result << ';';
        }
        result << '\n';
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    std::cout << "Full processing finished in: " << elapsed << " ms\n";

    return 0;
}
