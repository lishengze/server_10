#include "adk/encrypt_config.h"
#include "../ipc_call/ipc.h"
#include <boost/functional/hash.hpp>
#include <iostream>
#include <linux/limits.h>
#include <unistd.h>

namespace adk_impl
{

using namespace std;

constexpr int FileInfoLength = sizeof(ConfigFileInfo);
constexpr int HeaderLength   = sizeof(EncryptHeader);

void ArrayXOR(char* arr, int size)
{
    const unsigned char w[4] = {0x5f, 0x37, 0x59, 0xdf};
    for (int i = 0; i < size; i++)
    {
        arr[i] = arr[i] ^ w[i % 4];
    }
}

ConfigFile::ConfigFile()
{
    memset(&conf_header, 0, sizeof(conf_header));

    char exe_path[PATH_MAX] = {0};
    int res                 = readlink("/proc/self/exe", exe_path, PATH_MAX);
    if (res > 0)
    {
        out_file_.open(exe_path, ios::binary | ios::in);
        if (!out_file_.is_open())
            return;
        if (ReadHeader())
        {
            string md5_str = ReadFile("KEY1");
            if (md5_str.empty())
                return;
            aes_key = GenerateKey(conf_header.custom, md5_str);
        }
    }
}

ConfigFile::ConfigFile(const string& exe_path)
{
    memset(&conf_header, 0, sizeof(conf_header));

    if (!exe_path.empty())
    {
        out_file_.open(exe_path, ios::binary | ios::in | ios::out);
        if (!out_file_.is_open())
            return;
        if (ReadHeader())  // over-write
        {
            std::cout << "update old config file" << endl;
            string md5_str = ReadFile("KEY1");
            if (md5_str.empty())
                return;
            aes_key = GenerateKey(conf_header.custom, md5_str);
        }
    }
}

ConfigFile::ConfigFile(const string& exe_path, const string& custom, const string& md5_str, const string& version)
{
    memset(&conf_header, 0, sizeof(conf_header));

    if (!exe_path.empty())
    {
        out_file_.open(exe_path, ios::binary | ios::in | ios::out);
        if (!out_file_.is_open())
            return;
        if (ReadHeader())  // over-write
        {
            out_file_.seekp(0, ios::end);
            uint32_t length = out_file_.tellp();
            length -= conf_header.file_list_offset;

            char* zero = new char[length];
            memset(zero, 0, length);
            out_file_.seekp(conf_header.file_list_offset);
            out_file_.write(zero, length);
            delete[] zero;

            file_list.clear();
        }

        if (custom.size() >= sizeof(conf_header.custom))
        {
            std::cout << "custom must be less than " << sizeof(conf_header.custom) << "bytes" << std::endl;
            return;
        }
        if (version.size() >= sizeof(conf_header.version))
        {
            std::cout << "version must be less than " << sizeof(conf_header.version) << "bytes" << std::endl;
            return;
        }

        memset(&conf_header, 0, sizeof(conf_header));
        memcpy(conf_header.custom, custom.c_str(), custom.size());
        memcpy(conf_header.version, version.c_str(), version.size());
        if (md5_str.empty())
        {
            return;
        }
        aes_key = GenerateKey(custom, md5_str);
        WriteFile("KEY1", md5_str);
    }
}

// 不加密的接口
ConfigFile::ConfigFile(const string& exe_path, const std::string& version, bool is_encrypt)
{
    if (is_encrypt)
        return;

    memset(&conf_header, 0, sizeof(conf_header));

    if (!exe_path.empty())
    {
        out_file_.open(exe_path, ios::binary | ios::in | ios::out);
        if (!out_file_.is_open())
            return;
        if (ReadHeader())  // over-write
        {
            out_file_.seekp(0, ios::end);
            uint32_t length = out_file_.tellp();
            length -= conf_header.file_list_offset;

            char* zero = new char[length];
            memset(zero, 0, length);
            out_file_.seekp(conf_header.file_list_offset);
            out_file_.write(zero, length);
            delete[] zero;

            file_list.clear();
        }
        //清空存在原始的配置数据，清空头部结构体脏数据
        memset(&conf_header, 0, sizeof(conf_header));
        if (version.size() >= sizeof(conf_header.version))
        {
            std::cout << "version must be less than " << sizeof(conf_header.version) << "bytes" << std::endl;
            return;
        }
        memcpy(conf_header.version, version.c_str(), version.size());
    }
}

std::string ConfigFile::ReadConfigFile(const string& file_name)
{
    string encrypt_str = ReadFile(file_name);
    if (encrypt_str.empty())
    {
        return "";
    }
    return DecryptStr(encrypt_str);
}

bool ConfigFile::WriteConfigFile(const string& file_name, const string& config_str)
{
    if (!aes_key.empty())
    {
        string encrypt_str = EncryptStr(config_str);
        if (encrypt_str.empty())
        {
            cout << "encrypt string is empty" << endl;
            return false;
        }
        return WriteFile(file_name, encrypt_str);
    }

    else
        return false;
}

//无需解密获取字符串信息
std::string ConfigFile::ReadValue(const string& key)
{
    return ReadFile(key);
}

std::string ConfigFile::GetAppVersion()
{
    return ReadValue("APPVersion");
}

std::string ConfigFile::GetAMIVersion()
{
    return ReadValue("AMIVersion");
}

//不加密尾追字符串信息
bool ConfigFile::WriteStringPair(const string& key, const string& value)
{
    if (value.empty())
    {
        cout << "value string is empty" << endl;
        return false;
    }

    return WriteFile(key, value);
}

int ConfigFile::UpdateConfigFile(const string& file_name, const string& config_str)
{
    vector<ConfigFileInfo>::iterator info = FindConfigFile(file_name);
    if (info == file_list.end())
    {
        std::cout << "no config file named : " << file_name << std::endl;
        return 1;
    }

    if (!out_file_.is_open())
        return 2;

    if (!aes_key.empty())
    {
        string encrypt_str = EncryptStr(config_str);
        if (encrypt_str.empty())
            return 3;
        if (encrypt_str.size() > info->file_capacity)
        {
            std::cout << "file size is more than " << info->file_capacity << std::endl;
            return 4;
        }
        if (UpdateFile(info, encrypt_str))
        {
            return 0;
        }
        else
            return 5;
    }
    else
        return 6;
}

ConfigFile::FileIterator ConfigFile::FindConfigFile(const string& file_name)
{
    vector<ConfigFileInfo>::iterator info = file_list.begin();
    for (; info != file_list.end(); info++)
    {
        if (info->file_name == file_name)
        {
            break;
        }
    }
    return info;
}

string ConfigFile::GenerateKey(const string custom_str, string md5_str)
{
    if (md5_str.size() != 256)
        return "";

    uint8_t md5_code[256];
    uint8_t key[16];
    memcpy(md5_code, md5_str.c_str(), 256);
    memset(key, 0, 16);

    uint64_t custom_code = boost::hash_range(custom_str.begin(), custom_str.end());
    switch ((custom_code & 0x0000000030000000) >> 28)
    {
    case 0:
    {
        for (int i = 0; i < 16; i++)
        {
            for (int k = 0; k < 16; k += 4)
            {
                key[i] += ((md5_code[16 * i + k] & md5_code[16 * i + k + 1])
                           | (md5_code[16 * i + k + 2] & md5_code[16 * i + k + 3]));
            }
        }
        break;
    }
    case 1:
    {
        for (int i = 0; i < 16; i++)
        {
            for (int k = 0; k < 16; k += 4)
            {
                key[i] += ((md5_code[16 * i + k] & md5_code[16 * i + k + 2])
                           | (md5_code[16 * i + k + 1] & md5_code[16 * i + k + 3]));
            }
        }
        break;
    }
    case 2:
    {
        for (int i = 0; i < 16; i++)
        {
            for (int k = 0; k < 16; k += 4)
            {
                key[i] += ((md5_code[16 * i + k] & md5_code[16 * i + k + 3])
                           | (md5_code[16 * i + k + 1] & md5_code[16 * i + k + 2]));
            }
        }
        break;
    }
    case 3:
    {
        for (int i = 0; i < 16; i++)
        {
            for (int k = 0; k < 16; k += 4)
            {
                key[i] += ((md5_code[16 * i + k] ^ md5_code[16 * i + k + 1])
                           | (md5_code[16 * i + k + 2] & md5_code[16 * i + k + 3]));
            }
        }
        break;
    }
    }
    return string((char*)key, 16);
}

string ConfigFile::EncryptStr(const string& orig_str)
{
    Request req;
    req.SetMethodId(IpcMethod::kEncryptStr);
    req.Append(orig_str);
    req.Append(aes_key);

    Response rsp;
    IpcClient* ipc = IpcClient::GetIpc();
    if (nullptr == ipc)
    {
        //GetIpc返回nullptr,说明IPC子进程创建失败
        return std::string("");
    }
    if (IpcStatus::kIpcSucess != ipc->Call(req, rsp))
    {
        //Call返回值不为Sucess，可能是子进程异常退出，或 函数调用失败
        IpcClient::Exit();
        return std::string("");
    }
    std::string str = rsp.SubStr();
    IpcClient::Exit();
    return str;
}

string ConfigFile::DecryptStr(const string& encrypt_str)
{
    Request req;
    req.SetMethodId(IpcMethod::kDecryptStr);
    req.Append(encrypt_str);
    req.Append(aes_key);

    Response rsp;
    IpcClient* ipc = IpcClient::GetIpc();
    if (nullptr == ipc)
    {
        return std::string("");
    }
    if (IpcStatus::kIpcSucess != ipc->Call(req, rsp))
    {
        IpcClient::Exit();
        return std::string("");
    }
    std::string str = rsp.SubStr();
    IpcClient::Exit();

    return str;
}

bool ConfigFile::WriteFile(const string& file_name, const string& config_str)
{
    if (FindConfigFile(file_name) != file_list.end())
    {
        std::cout << "config file has existed" << std::endl;
        return false;
    }

    if (!out_file_.is_open())
    {
        cout << " out_file is not open" << endl;
        return false;
    }

    if (file_name.size() >= 256)
    {
        std::cout << "file name must less 255 bytes" << std::endl;
        return false;
    }

    out_file_.seekp(0, ios::end);

    if (conf_header.file_num == 0)
    {
        conf_header.file_list_offset = out_file_.tellp();
    }
    conf_header.file_num++;

    ConfigFileInfo info;
    memcpy(info.file_name, file_name.c_str(), file_name.size());  //检查长度
    info.file_size     = config_str.size();
    info.file_capacity = info.file_size + 100;

    file_list.push_back(info);

    char buff[FileInfoLength];
    memcpy(buff, &info, FileInfoLength);

    out_file_.write(buff, FileInfoLength);

    out_file_.write(config_str.c_str(), config_str.size());
    char space[100];
    memset(space, ' ', sizeof(space));
    out_file_.write(space, 100);

    return true;
}

bool ConfigFile::UpdateFile(FileIterator info, const string& config_str)
{
    if (!out_file_.is_open())
        return false;
    uint32_t stream_pos = conf_header.file_list_offset;
    for (auto it = file_list.begin(); it != file_list.end(); it++)
    {
        if (it == info)
        {
            break;
        }
        stream_pos += FileInfoLength;
        stream_pos += it->file_capacity;
    }

    info->file_size = config_str.size();

    char buff[FileInfoLength];
    memcpy(buff, info->file_name, sizeof(info->file_name));
    memcpy(buff + sizeof(info->file_name), (void*)&info->file_size, sizeof(info->file_size));
    memcpy(buff + sizeof(info->file_name) + sizeof(info->file_size), (void*)&info->file_capacity, sizeof(info->file_capacity));

    out_file_.seekp(stream_pos, ios::beg);
    out_file_.write(buff, FileInfoLength);

    out_file_.write(config_str.c_str(), config_str.size());

    uint32_t space_len = info->file_capacity - info->file_size;
    char* space        = new char[space_len];
    memset(space, ' ', space_len);
    out_file_.write(space, space_len);
    out_file_.flush();
    delete[] space;
    return true;
}

std::string ConfigFile::ReadFile(const string& file_name)
{
    if (conf_header.file_num == 0)
        return "";
    if (!out_file_.is_open())
        return "";

    uint32_t stream_pos = conf_header.file_list_offset;

    auto info = file_list.begin();
    for (; info != file_list.end(); info++)
    {
        if (0 == strcmp(info->file_name, file_name.c_str()))
        {
            break;
        }
        stream_pos += FileInfoLength;
        stream_pos += info->file_capacity;
    }
    if (info == file_list.end())
        return "";

    out_file_.seekg(stream_pos + FileInfoLength, ios::beg);
    char* buff            = new char[info->file_size + 1];
    buff[info->file_size] = '\0';
    out_file_.read(buff, info->file_size);
    string str(buff, info->file_size);
    delete[] buff;
    return str;
}

bool ConfigFile::WriteHeader()
{
    if (!out_file_.is_open())
        return false;

    conf_header.file_num = file_list.size();

    memcpy(conf_header.eigen_val, "ArchConf", 8);
    ArrayXOR(conf_header.eigen_val, 8);
    ArrayXOR(conf_header.custom, sizeof(conf_header.custom));

    char header_buff[HeaderLength];
    memcpy(header_buff, &conf_header, HeaderLength);
    if (ReadHeader())
    {
        out_file_.seekp(0 - HeaderLength, ios::end);
    }
    else
    {
        out_file_.seekp(0, ios::end);
    }
    out_file_.write(header_buff, HeaderLength);

    return true;
}

bool ConfigFile::ReadHeader()
{
    if (!out_file_.is_open())
        return false;

    // check file size
    out_file_.seekp(0, ios::end);
    uint32_t length = out_file_.tellp();
    if (length < HeaderLength)
        return false;

    out_file_.seekg(0 - HeaderLength, ios::end);
    char header_buff[HeaderLength];

    out_file_.read(header_buff, HeaderLength);

    char* const _header_buff = header_buff;
    conf_header              = *(reinterpret_cast<EncryptHeader*>(_header_buff));

    //check
    ArrayXOR(conf_header.eigen_val, sizeof(conf_header.eigen_val));
    if (memcmp(conf_header.eigen_val, "ArchConf", 8) != 0)
    {
        return false;
    }
    ArrayXOR(conf_header.custom, sizeof(conf_header.custom));

    file_list.clear();
    out_file_.seekg(conf_header.file_list_offset, ios::beg);

    uint32_t stream_pos = conf_header.file_list_offset;
    char info_buff[FileInfoLength];

    for (uint32_t i = 0; i < conf_header.file_num; i++)
    {
        ConfigFileInfo info;
        memset(info_buff, 0, FileInfoLength);

        out_file_.read(info_buff, FileInfoLength);
        stream_pos += FileInfoLength;

        char* const _info_buff = info_buff;
        info                   = *(reinterpret_cast<ConfigFileInfo*>(_info_buff));

        stream_pos += info.file_capacity;
        out_file_.seekg(stream_pos);

        file_list.push_back(info);
    }

    return true;
}

bool ConfigFile::GetHeader(EncryptHeader* header)
{
    if (ReadHeader() == true)
    {
        memcpy(header, &conf_header, sizeof(conf_header));
        ArrayXOR(header->eigen_val, sizeof(conf_header.eigen_val));
        ArrayXOR(header->custom, sizeof(conf_header.custom));
        return true;
    }
    return false;
}

std::string ConfigFile::GetVersion()
{
    return string(conf_header.version);
}

ConfigFile::~ConfigFile()
{
    if (out_file_.is_open())
        out_file_.close();
}

}