#include "evo/CSV.h"
#include "gtest/gtest.h"
#include <filesystem>


using namespace evo;

class CSVParserTests : public ::testing::Test {
public:
    NdArray<double> dataset1 = {{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}};
    StringVector column_names = {"C1", "C2", "C3"};
    StringVector index_names = {"R1", "R2"};

    std::string data_both = "idx, C1, C2, C3\n"
                            "R1, 1.0, 2.0, 3.0\n"
                            "R2, 4.0, 5.0, 6.0\n";

    std::string data_headers = "C1, C2, C3\n"
                               "1.0, 2.0, 3.0\n"
                               "4.0, 5.0, 6.0\n";

    std::string data_neither = "1.0, 2.0, 3.0\n"
                               "4.0, 5.0, 6.0\n";


    std::filesystem::path fname_both = std::filesystem::current_path() /= "data_headers_and_index.csv";
    std::filesystem::path fname_headers_only = std::filesystem::current_path() /= "data_headers_only.csv";
    std::filesystem::path fname_neither = std::filesystem::current_path() /= "data_no_headers_or_index.csv";

    CSVParserTests() {
        std::cout << "writing to " << fname_both.string() << std::endl;
        std::ofstream fstream_both;
        fstream_both.open(fname_both.string());
        if (!fstream_both) {// file couldn't be opened
            std::cerr << "Error: file could not be opened" << std::endl;
            exit(1);
        }
        fstream_both << data_both << std::endl;
        fstream_both.close();

        std::ofstream fstream_headers;
        fstream_headers.open(fname_headers_only.string());
        if (!fstream_headers) {// file couldn't be opened
            std::cerr << "Error: file could not be opened" << std::endl;
            exit(1);
        }
        fstream_headers << data_headers << std::endl;
        fstream_headers.close();

        std::ofstream fstream_neither;
        fstream_neither.open(fname_neither.string());
        if (!fstream_neither) {// file couldn't be opened
            std::cerr << "Error: file could not be opened" << std::endl;
            exit(1);
        }
        fstream_neither << data_neither << std::endl;
        fstream_neither.close();
    };

    ~CSVParserTests() override {
        bool CLEAN_UP = true;
        if (CLEAN_UP) {
            if (std::filesystem::exists(fname_both))
                remove(fname_both.string().c_str());

            if (std::filesystem::exists(fname_headers_only))
                remove(fname_headers_only.string().c_str());

            if (std::filesystem::exists(fname_neither))
                remove(fname_neither.string().c_str());
        }
    }
};


TEST_F(CSVParserTests, TestParserDimensions) {
    CSV parser(fname_both);
    ASSERT_EQ(2, parser.getNRows());
    ASSERT_EQ(3, parser.getNCols());

}

TEST_F(CSVParserTests, TestParserData) {
    CSV parser(fname_both);
    const NdArray<double>&actual = parser.getData();
    NdArray<double> expected = {{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}};
    ASSERT_TRUE(nc::all(nc::equal(expected, actual))(0, 0));
}

TEST_F(CSVParserTests, TestColumns) {
    CSV parser(fname_both);
    const StringVector& actual = parser.colNames();
    StringVector expected = {"R1", "R2"};
    ASSERT_EQ(expected, actual);
}

TEST_F(CSVParserTests, TestRows) {
    CSV parser(fname_both);
    const StringVector& actual = parser.rowNames();
    StringVector expected = {"R1", "R2"};
    ASSERT_EQ(expected, actual);
}

TEST_F(CSVParserTests, TestNRows) {
    CSV parser(fname_both);
    ASSERT_EQ(2, parser.getNRows());
}

TEST_F(CSVParserTests, TestNCols) {
    CSV parser(fname_both);
    ASSERT_EQ(3, parser.getNCols());
}

TEST_F(CSVParserTests, TestToCSV) {
    CSV parser(fname_both);
    std::string actual = parser.toCSV().str();
    std::string expected = "idx,  C1,  C2,  C3\n"
                           "R1, 1, 2, 3\n"
                           "R2, 4, 5, 6\n";
    ASSERT_STREQ(expected.c_str(), actual.c_str());
}


// remember to handle missing data