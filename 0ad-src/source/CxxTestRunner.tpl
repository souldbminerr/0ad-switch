// vim: set filetype=cpp :

#include <cstring>
#include <cxxtest/Descriptions.h>
#include <cxxtest/ErrorPrinter.h>
#include <cxxtest/XmlPrinter.h>
#include <iostream>
#include <ps/DllLoader.h>
#include <string>

void usage(const char* prog) {
	const std::string help = std::string("Usage: ") + prog  + R"( [options]

--format    junit|simple    simple: default, good for cli - junit: for use by CI
--help                      This message
--libdir    dir             Where to find extra shared libraries, used for running tests in staging area
--list                      List available test by suite
--output    file            File to write to, defaults to stdout
--suite     suite           Only run named suite
--test      test            Only run named test of given suite
)";
	std::printf("%s\n", help.c_str());
}

int main(int argc, char **argv)
{

	char* suite = nullptr;
	char* test = nullptr;
	bool xml = false;
	std::ostream* output = &std::cout;
    std::ofstream out;

	for (int i = 1; i < argc; ++i)
	{
		if (std::strcmp(argv[i], "--libdir") == 0)
		{
			if (++i >= argc)
			{
				std::printf("Option libdir requires an optarg\n\n");
				usage(argv[0]);
				std::exit(1);
			}

			DllLoader::OverrideLibdir(argv[i]);
		}
		else if (std::strcmp(argv[i], "--format") == 0)
		{
			if (++i >= argc)
			{
				std::printf("Option format requires an optarg\n\n");
				usage(argv[0]);
				std::exit(1);
			}

			if (std::strcmp(argv[i], "junit") == 0)
				xml  = true;
			else if (std::strcmp(argv[i], "simple") == 0)
				xml = false;
			else
			{
				std::printf("Unkown format: %s\n\n", argv[i]);
				usage(argv[0]);
				std::exit(1);
			}
		}
		else if (std::strcmp(argv[i], "--output") == 0)
		{
			if (++i >= argc)
			{
				std::printf("Option output requires an optarg\n\n");
				usage(argv[0]);
				std::exit(1);
			}

			out.open(argv[i]);
			output = &out;
		}
		else if (std::strcmp(argv[i], "--list") == 0)
		{
			for (CxxTest::SuiteDescription *sd = CxxTest::RealWorldDescription().firstSuite(); sd; sd = sd->next())
			{
				std::printf("%s\n", sd->suiteName());
				for (CxxTest::TestDescription *td = sd->firstTest(); td; td = td->next())
					std::printf("    %s\n", td->testName());
			}
			std::exit(0);
		}
		else if (std::strcmp(argv[i], "--suite") == 0)
		{
			if (++i >= argc)
			{
				std::printf("Option suite requires an optarg\n\n");
				usage(argv[0]);
				std::exit(1);
			}
			suite = argv[i];
		}
		else if (std::strcmp(argv[i], "--test") == 0)
		{
			if (++i >= argc)
			{
				std::printf("Option test requires an optarg\n\n");
				usage(argv[0]);
				std::exit(1);
			}
			test = argv[i];
		}
		else if (std::strcmp(argv[i], "--help") == 0)
		{
			usage(argv[0]);
			std::exit(0);
		}
		else
		{
			std::printf("Unkown argument: %s\n\n", argv[i]);
			usage(argv[0]);
			std::exit(1);
		}
	}

	if (test)
	{
		if (!suite)
		{
			std::printf("test needs a suite specified\n\n");
			usage(argv[0]);
			std::exit(1);
		}
		if (!CxxTest::leaveOnly(suite, test))
		{
			std::printf("Unknown test '%s' in suite '%s'\n\n", test, suite);
			usage(argv[0]);
			std::exit(1);
		}
	}
	else if (suite)
	{
		if (!CxxTest::leaveOnly(suite))
		{
			std::printf("Unknown suite '%s'\n\n", suite);
			usage(argv[0]);
			std::exit(1);
		}
	}

	if (xml)
		return CxxTest::XmlPrinter(*output).run();

	return CxxTest::ErrorPrinter(*output).run();
}

// The CxxTest "world"
<CxxTest world>
