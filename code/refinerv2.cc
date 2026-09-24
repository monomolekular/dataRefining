#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <unordered_set>
#include <map>
#include <chrono>
#include <ctime>
#include <algorithm>

struct Data{
	int id;
	std::string name;
	std::string date;
	mutable std::vector<float> values;

	bool operator == (const Data& other) const {
		return name == other.name && date == other.date;
	}
};

struct LogKey {
	std::string name;
	std::string date;
	bool operator<(const LogKey& other) const {
	        if (name != other.name)
	            return name < other.name;
	        std::string d1 = date.substr(6,4) + date.substr(3,2) + date.substr(0,2);
	        std::string d2 = other.date.substr(6,4) + other.date.substr(3,2) + other.date.substr(0,2);
	        return d1 < d2;
	}
};


struct DataHasher{
	std::size_t operator()(const Data& item) const {
		return std::hash<std::string>()(item.name) ^ (std::hash<std::string>()(item.date) << 1);
	}
};

void filterByDate(std::vector<Data>& allData, const std::string& date1, const std::string& date2){
	std::string start = date1.substr(6,4) + date1.substr(3,2) + date1.substr(0,2);
	std::string end = date2.substr(6,4) + date2.substr(3,2) + date2.substr(0,2);
	std::vector<Data> filteredData;
	for(const auto& item : allData){
		std::string currDate = item.date.substr(6,4) + item.date.substr(3,2) + item.date.substr(0,2);
		if( currDate >= start && currDate <= end )
			filteredData.push_back(item);	
	}
	allData = std::move(filteredData);
}

unsigned long long countRows1(std::ifstream& file){
	if (file.peek() == std::ifstream::traits_type::eof()) return 0;
	auto rows = std::count_if(std::istreambuf_iterator<char>{file}, {}, [](char c) { return c == '\n'; });
	return rows;
}

unsigned long long countRows2(std::ifstream& file){
	if (file.peek() == std::ifstream::traits_type::eof()) return 0;
	unsigned long long rows = 0;
    std::vector<char> buffer(64 * 1024);
    char lastChar = '\0';
    while (file.read(buffer.data(), buffer.size()) || file.gcount() > 0) {
        auto bytesRead = file.gcount();
        rows += std::count(buffer.begin(), buffer.begin() + bytesRead, '\n');
        lastChar = buffer[bytesRead - 1];
    }
    if (lastChar != '\n') rows++; 
    return rows;
}

int main(int argc, char** argv){
	std::ofstream timeLogger("timeLog.txt", std::ios::app);
	auto startTime = std::chrono::system_clock::now();
	if(argc < 2)return 1;
	std::ofstream result("OutputData.csv");
	result << "id;name;date;value1;value2;value3;value4;value5;value6;value7\n";
	std::vector<Data> allData;	
	for(int i=1; i<argc; ++i){
		std::ifstream file(argv[i]);
		if(!file.is_open()) return 1;	
		std::string line;
		std::getline(file,line);
		while(std::getline(file,line)){
			std::stringstream ss(line);
			Data row;	
        
		        std::string s_id, s_value;
	
	        	std::getline(ss, s_id, ';');
		        std::getline(ss, row.name, ';');
		        std::getline(ss, row.date, ';');

	        	row.id = std::stoi(s_id);
	
		        for (int i = 0; i < 7; ++i) {
		            if (std::getline(ss, s_value, ';'))
	        	        row.values.push_back(std::stof(s_value));
		        }
		        allData.push_back(row);	
		}
	}
	auto endTime1 = std::chrono::system_clock::now();
	auto elapsed1 = std::chrono::duration_cast<std::chrono::milliseconds>(endTime1-startTime).count();

	filterByDate(allData, "01.09.2026", "31.12.2026");
	
	auto endTime2 = std::chrono::system_clock::now();
	auto elapsed2 = std::chrono::duration_cast<std::chrono::milliseconds>(endTime2-endTime1).count();
	
	std::unordered_set<Data, DataHasher> holder;
	std::ofstream log("log.txt");
	for(const auto& data: allData){
		auto it = holder.find(data);
		if(it != holder.end()){
			log << "found duplicate!\n";
			const auto& existingItem = *it;
			for(size_t i = 0; i < existingItem.values.size(); ++i)
				existingItem.values[i] += data.values[i];
		}
		else holder.insert(data);
		
	}
	
	std::map<LogKey, Data> tree;
	for(const auto& data: holder){
		LogKey key = {data.name, data.date};
		tree[key] = data;
	}
	
	for (const auto& pair : tree) {
		const auto& item = pair.second;
		result << item.id << ';' << item.name << ';' << item.date << ';';
	        for (size_t j = 0; j < item.values.size(); ++j) {
			result << item.values[j];
			if (j < item.values.size() - 1) result << ';';
		}

		result << '\n';
		
    	}
	auto endTime3 = std::chrono::system_clock::now();
	auto elapsed3 = std::chrono::duration_cast<std::chrono::milliseconds>(endTime3-endTime2).count();
	auto now = std::time(nullptr);
	auto nowToo = std::string(std::ctime(&now));
	nowToo.pop_back();
	timeLogger << '\n' << nowToo << ": merging for: " << elapsed1 << " ms; filtered for " << elapsed2 << " ms; sorted for " << elapsed3 << " ms\n";
	
	return 0;
	// const std::string filename = "IOfiles/data10Mil.csv"; // Имя вашего тестового файла

    // // --- ТЕСТ 1: countRows1 ---
    // std::ifstream file1(filename, std::ios::binary);
    // if (!file1) {
    //     std::cerr << "Error opening file for Test 1" << std::endl;
    //     return 1;
    // }

    // auto start1 = std::chrono::high_resolution_clock::now();
    // unsigned long long res1 = countRows1(file1);
    // auto end1 = std::chrono::high_resolution_clock::now();
    // file1.close();

    // std::chrono::duration<double, std::milli> duration1 = end1 - start1;
    // std::cout << "[Test 1] count_if + iterator:\n"
    //           << "  Rows count: " << res1 << "\n"
    //           << "  Time taken: " << duration1.count() << " ms\n\n";

    // // --- ТЕСТ 2: countRows2 ---
    // std::ifstream file2(filename, std::ios::binary);
    // if (!file2) {
    //     std::cerr << "Error opening file for Test 2" << std::endl;
    //     return 1;
    // }

    // auto start2 = std::chrono::high_resolution_clock::now();
    // unsigned long long res2 = countRows2(file2);
    // auto end2 = std::chrono::high_resolution_clock::now();
    // file2.close();

    // std::chrono::duration<double, std::milli> duration2 = end2 - start2;
    // std::cout << "[Test 2] Block buffer + std::count:\n"
    //           << "  Rows count: " << res2 << "\n"
    //           << "  Time taken: " << duration2.count() << " ms\n\n";

    // // Вывод разницы
    // if (duration2.count() > 0) {
    //     std::cout << "Method 2 is " << (duration1.count() / duration2.count()) << "x faster!\n";
    // }

    // return 0;
}
