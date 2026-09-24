#include <iostream>
#include <fstream>
#include <vector>
#include <random>
#include <charconv>
#include <string>
#include <cstdint>
#include <chrono>

// Инициализируем генератор временем, это никогда не зависнет под Windows/MinGW
static std::mt19937& get_gen() {
    thread_local std::mt19937 gen(static_cast<unsigned int>(
        std::chrono::system_clock::now().time_since_epoch().count()
    ));
    return gen;
}

inline void appendRandomString(std::vector<char>& buf, int minLen, int maxLen) {
    std::uniform_int_distribution<int> dist(minLen, maxLen);
    std::uniform_int_distribution<int> charDist('a', 'z');
    int len = dist(get_gen());
    for (int i = 0; i < len; ++i) {
        buf.push_back(static_cast<char>(charDist(get_gen())));
    }
}

inline void appendRandomDate(std::vector<char>& buf, int startYear, int endYear) {
    std::uniform_int_distribution<int> yearDist(startYear, endYear);
    std::uniform_int_distribution<int> monthDist(1, 12);
    std::uniform_int_distribution<int> dayDist(1, 28);

    int year = yearDist(get_gen());
    int month = monthDist(get_gen());
    int day = dayDist(get_gen());

    // Ручная быстрая сборка даты
    buf.push_back(static_cast<char>('0' + (day / 10)));
    buf.push_back(static_cast<char>('0' + (day % 10)));
    buf.push_back('.');
    buf.push_back(static_cast<char>('0' + (month / 10)));
    buf.push_back(static_cast<char>('0' + (month % 10)));
    buf.push_back('.');
    
    // Год из 4 цифр
    buf.push_back(static_cast<char>('0' + (year / 1000)));
    buf.push_back(static_cast<char>('0' + ((year % 1000) / 100)));
    buf.push_back(static_cast<char>('0' + ((year % 100) / 10)));
    buf.push_back(static_cast<char>('0' + (year % 10)));
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <number_of_rows>\n";
        return 1;
    }

    int rowCount = std::stoi(argv[1]);
    std::cout << "Starting generation of " << rowCount << " rows..." << std::endl;
    
    // std::ios::trunc принудительно перезапишет файл, если он существует
    std::ofstream file("data(generated).csv", std::ios::binary | std::ios::trunc);
    if (!file) {
        std::cerr << "Failed to create file.\n";
        return 1;
    }

    file << "id;name;date;value1;value2;value3;value4;value5;value6;value7\n";

    std::uniform_real_distribution<float> floatDist(0.0001f, 100000.0f);

    constexpr size_t bufferSize = 64 * 1024 * 1024; // 64 MB
    std::vector<char> buffer;
    buffer.reserve(bufferSize);

    char numStr[32];

    for (int i = 0; i < rowCount; ++i) {
        // 1. ID через to_chars (для целых чисел работает идеально везде)
        auto res = std::to_chars(numStr, numStr + 32, i);
        buffer.insert(buffer.end(), numStr, res.ptr);
        buffer.push_back(';');

        // 2. Случайная строка
        appendRandomString(buffer, 5, 10);
        buffer.push_back(';');

        // 3. Случайная дата
        appendRandomDate(buffer, 2005, 2026);
        buffer.push_back(';');

        // 4. 7 значений float через стабильный to_chars без фиксированного флага
        // (убираем chars_format::fixed, чтобы избежать старых багов MinGW)
        for (int j = 0; j < 7; ++j) {
            float val = floatDist(get_gen());
            res = std::to_chars(numStr, numStr + 32, val);
            buffer.insert(buffer.end(), numStr, res.ptr);
            
            if (j < 6) buffer.push_back(';');
        }
        buffer.push_back('\n');

        // Сброс буфера на диск, когда он почти заполнен
        if (buffer.size() >= bufferSize - 65536) {
            file.write(buffer.data(), buffer.size());
            buffer.clear();
        }
    }

    if (!buffer.empty()) {
        file.write(buffer.data(), buffer.size());
    }

    std::cout << "Successfully generated and saved to 'data(generated).csv'!" << std::endl;
    return 0;
}
