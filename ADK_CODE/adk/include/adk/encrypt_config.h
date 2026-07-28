#ifndef ADK_IMPL_CONFIG_FILE_H
#define ADK_IMPL_CONFIG_FILE_H

#include <fstream>
#include <string>
#include <vector>
#include <stdlib.h>
#include <iomanip>
#include <string.h>

namespace adk_impl
{

using std::string;
using std::vector;

#pragma pack(push)
#pragma pack(4)

struct ConfigFileInfo
{
	char file_name[256];
	uint32_t file_size;
	uint32_t file_capacity;
	ConfigFileInfo()
	{
		memset(file_name, 0, sizeof(file_name));
		file_size = 0;
		file_capacity = 0;
	}
};

struct EncryptHeader
{
	uint32_t file_num;
	uint32_t file_list_offset;
	char eigen_val[8];          //特征值
	char version[256];			//版本号
	char custom[64];            //客户号
};


class ConfigFile
{
public:
	ConfigFile();	//ReadConfig
	ConfigFile(const std::string& exe_path);	//UpdateConfig
	ConfigFile(const std::string& exe_path, const std::string& custom, const std::string& md5_str, const std::string& version);	//Write
	ConfigFile(const std::string& exe_path, const std::string& version, bool is_encrypt);	// 不加密
	
	std::string ReadValue(const std::string& key);
	bool WriteStringPair(const std::string& key, const std::string& value);
	
	std::string GetAppVersion();
	std::string GetAMIVersion();

	std::string ReadConfigFile(const std::string& file_name);
	bool WriteConfigFile(const std::string& file_name, const std::string& config_str);
	int UpdateConfigFile(const std::string& file_name, const std::string& config_str);
	
	bool WriteHeader();
	bool GetHeader(EncryptHeader* header);
	std::string GetVersion();	//获取头部信息中的version
	~ConfigFile();
private:
	using FileIterator = std::vector<ConfigFileInfo>::iterator;
	FileIterator FindConfigFile(const std::string& file_name);
	std::string GenerateKey(const std::string custom_str, std::string md5_str);
	std::string EncryptStr(const std::string& orig_str);
	std::string DecryptStr(const std::string& orig_str);
	bool WriteFile(const std::string& file_name, const std::string& config_str);
	bool UpdateFile(FileIterator info, const std::string& config_str);
	std::string ReadFile(const std::string& file_name);
	bool ReadHeader();

	std::string aes_key;
	EncryptHeader conf_header;
	std::fstream out_file_;
	std::vector<ConfigFileInfo> file_list;
};
#pragma pack(pop)

}
#endif