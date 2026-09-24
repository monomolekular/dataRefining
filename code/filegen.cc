#include<iostream>
#include<fstream>
#include<sstream>
#include<iomanip>
#include<chrono>
#include<random>


std::random_device rd;
std::mt19937 gen(rd());

int getRandomNumber(int min, int max) {
    	std::uniform_int_distribution<int> distrib(min, max);
	return distrib(gen);
}

float getRandomFloat(float min, float max){
	std::uniform_real_distribution<float> distrib(min, max);
	return distrib(gen);
}

std::stringstream getRandomDate(int startYear, int endYear){
	std::stringstream ret;
	std::tm start_tm = {};	
    	start_tm.tm_year = startYear - 1900;
    	start_tm.tm_mon = 0;                
    	start_tm.tm_mday = 1;               
    
    	std::tm end_tm = {};
    	end_tm.tm_year = endYear - 1900;
	end_tm.tm_mon = 11;                 
    	end_tm.tm_mday = 31;                

    	auto start_time = std::chrono::system_clock::from_time_t(std::mktime(&start_tm));
    	auto end_time = std::chrono::system_clock::from_time_t(std::mktime(&end_tm));

    	auto duration_sec = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count();
    
    	// Генерируем случайное смещение в секундах
    	int random_sec = getRandomNumber(0, duration_sec);
    
    	auto random_time = start_time + std::chrono::seconds(random_sec);
    	std::time_t result_c_time = std::chrono::system_clock::to_time_t(random_time);
    
    	// Выводим результат в читаемом формате
    	std::tm* result_tm = std::localtime(&result_c_time);
    	ret << std::put_time(result_tm, "%d.%m.%Y");
	return ret;
}

std::stringstream getRandomString(int a, int b) {
    std::stringstream ss;
    int len = getRandomNumber(a, b);
    while (len--)
        ss << (char)getRandomNumber('a','z');
    return ss;
}


int main(int argc, char** argv){
	if(argc != 2){
		std::cerr << "Usage: input number of random data u want to generate";
		return 1;
	}
	std::ofstream file("data(generated).csv");
	file << "id;name;date;value1;value2;value3;value4;value5;value6;value7\n";
	int rowCount = std::stoi(argv[1]);
	for(int i=0; i< rowCount; ++i){
		file << i << ';' << getRandomString(5,10).str() << ';' << getRandomDate(2005,2026).str() << ';';
		for(int j=0;j<7;++j){
			file << getRandomFloat(0.0001, 100000);
			if(j<6) file << ';';
		}
		file <<'\n';
	}
	return 0;
}
