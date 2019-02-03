
#include <boost/filesystem.hpp>
#include <boost/core/lightweight_test.hpp>
#include <vector>

namespace fs = boost::filesystem;

void run()
{
    const fs::path cwd = fs::current_path();
    const fs::path junction = cwd / "junction";
    const fs::path real = cwd / "real";
    const fs::path subDir = "sub";
    fs::create_directories(real / sub);
    BOOST_TEST(std::system("mklink /j junction real") == 0);
    BOOST_TEST(fs::exists(junction));

    // Due to a bug there was a dependency on the current path so try the below for all:
    std::vector<fs::path> paths;
    paths.push_back(cwd);
    paths.push_back(junction);
    paths.push_back(real);
    paths.push_back(junction/sub);
    paths.push_back(real/sub);
    for(std::vector<fs::path>::iterator it = paths.begin(); it != paths.end(); ++it)
    {
        fs::current_path(*it);

        // Used by canonical, must work too
        BOOST_TEST(fs::read_symlink(junction) == real);

        BOOST_TEST(fs::canonical(junction) == real);
        BOOST_TEST(fs::canonical(junction / sub) == real / sub);
    }
}

int main()
{
    if(BOOST_PLATFORM == "Windows")
        run();
    return boost::report_errors();
}

