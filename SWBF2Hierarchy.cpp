#include <iostream>
#include <fstream>
#include <vector>
#include <filesystem>
#include <LibSWBF2.h>
#include <fmt/format.h>
#include <graphviz/gvc.h>
#include <graphviz/xdot.h>
#include "CLI11.hpp"

namespace fs = std::filesystem;

std::set<std::string> RootClasses;

void Log(const char* msg, bool overrideLine=false)
{
	if (overrideLine)
	{
		char space[80];
		memset(space, ' ', 79);
		space[79] = 0;	// null termination
		std::cout << '\r' << space << '\r' << msg << std::endl;
	}
	else
	{
		std::cout << msg << std::endl;
	}
}

void Log(const std::string& msg, bool overrideLine=false)
{
	Log(msg.c_str(), overrideLine);
}

std::vector<fs::path> GetFiles(const fs::path& Directory, const std::string& Extension, const bool recursive)
{
	std::vector<fs::path> mshFiles;
	if (!fs::exists(Directory))
	{
		return mshFiles;
	}

	for (auto p : fs::directory_iterator(Directory))
	{
		if (p.path().extension() == Extension)
		{
			mshFiles.insert(mshFiles.end(), p.path().string());
		}

		if (recursive && p.is_directory())
		{
			const std::vector<fs::path>& next = GetFiles(p.path(), Extension, recursive);
			mshFiles.insert(mshFiles.end(), next.begin(), next.end());
		}
	}
	return mshFiles;
}

std::vector<fs::path> GetFiles(const std::vector<fs::path>& Paths, const std::string& Extension, const bool recursive)
{
	std::vector<fs::path> files;
	for (auto it = Paths.begin(); it != Paths.end(); ++it)
	{
		if (fs::exists(*it))
		{
			if (fs::is_directory(*it))
			{
				const std::vector<fs::path>& next = GetFiles(*it, Extension, recursive);
				files.insert(files.end(), next.begin(), next.end());
			}
			else if (fs::is_regular_file(*it))
			{
				files.insert(files.end(), *it);
			}
		}
		else
		{
			Log((*it).u8string() + " does not exist!");
		}
	}
	return files;
}

bool PullLibMesages()
{
	bool thereWasALog = false;
	LibSWBF2::Logging::LoggerEntry log;
	while (LibSWBF2::Logging::Logger::GetNextLog(log))
	{
		Log("[LibSWBF2] " + std::string(log.ToString().Buffer()));
		thereWasALog = true;
	}
	return thereWasALog;
}

std::string GetRootClassName(const LibSWBF2::Wrappers::EntityClass& ec)
{
	const auto* base = ec.GetBase();
	const auto& baseName = ec.GetBaseName();
	if (base != nullptr)
	{
		return GetRootClassName(*base);
	}
	return baseName.Buffer();
}

void CrawlNodesRecursive(const LibSWBF2::Wrappers::EntityClass& ec)
{
	const auto* base = ec.GetBase();
	const auto& baseName = ec.GetBaseName();
	if (base != nullptr && !baseName.IsEmpty())
	{
		CrawlNodesRecursive(*base);
	}
	else if (!baseName.IsEmpty())
	{
		RootClasses.emplace(baseName.Buffer());
	}
}

Agnode_t* AddNodeRecursive(Agraph_t* graph, std::map<std::string, Agnode_t*>& classNodes, const std::string& rootClassName, const LibSWBF2::Wrappers::EntityClass& ec)
{
	const auto& name = ec.GetTypeName();
	auto it = classNodes.find(name.Buffer());
	if (it != classNodes.end())
	{
		return it->second;
	}

	std::string rootName = GetRootClassName(ec);
	if (rootName != rootClassName)
	{
		return nullptr;
	}

	Agnode_t* node = agnode(graph, (char*)name.Buffer(), 1);
	Log(fmt::format("Added node '{0}'", name.Buffer()));

	const auto* base = ec.GetBase();
	const auto& baseName = ec.GetBaseName();
	if (base != nullptr)
	{
		Agnode_t* baseNode = AddNodeRecursive(graph, classNodes, rootClassName, *base);
		if (baseNode != nullptr)
		{
			agedge(graph, baseNode, node, nullptr, 1);
		}
	}
	else if (!baseName.IsEmpty())
	{
		Agnode_t* baseNode;

		auto it2 = classNodes.find(baseName.Buffer());
		if (it2 != classNodes.end())
		{
			// root class already has a node
			baseNode = it2->second;
		}
		else
		{
			baseNode = agnode(graph, (char*)baseName.Buffer(), 1);
		}

		agedge(graph, baseNode, node, nullptr, 1);
		classNodes.emplace(baseName.Buffer(), baseNode);
	}

	return node;
}

int main(int argc, char* argv[])
{
	CLI::App app
	{
		"---------------------------------------------------------\n"
		"-------------------- SWBF2 Hierarchy --------------------\n"
		"---------------------------------------------------------\n"
		"Web: https://github.com/Ben1138/SWBF2Hierarchy \n"
		"\n"
		"This tool plots the entity class hierarchy of all given *.lvl files."
	};

	std::vector<fs::path> files;
	std::vector<std::string> customClasses;

	CLI::Option* filesOpt = app.add_option("-f,--files", files, "LVL file paths (file or directory, one or more)")->check(CLI::ExistingPath);
	CLI::Option* classOpt = app.add_option("-c,--rootClass", customClasses, "Only plot the given root classes");
	CLI::Option* recOpt = app.add_flag("-r,--recursive", "For all given directories, crawling will be recursive (will include all sub-directories)");

	// *parse magic*
	CLI11_PARSE(app, argc, argv);

	// Nothing to do if no lvl files are given
	if (files.size() == 0)
	{
		Log("No LVL files given!");
		Log(app.help());
		return 0;
	}

	std::vector<fs::path> lvlFiles = GetFiles(files, ".lvl", recOpt->count() > 0);

	LibSWBF2::Logging::Logger::SetLogfileLevel(LibSWBF2::ELogType::Warning);
	LibSWBF2::Container* con = LibSWBF2::Container::Create();

	std::vector<LibSWBF2::SWBF2Handle> lvls;
	for (const fs::path& path : lvlFiles)
	{
		lvls.emplace_back(con->AddLevel(path.u8string().c_str()));
		Log(fmt::format("Schedule LVL: {0}", path.u8string()));
	}
	con->StartLoading();

	while (!con->IsDone())
	{
		if (PullLibMesages())
		{
			// do not override the last lib log with out progress
			Log("");
		}
		Log(fmt::format("{0} %", int(con->GetOverallProgress() * 100.0f)).c_str(), true);
	}
	PullLibMesages();

	Agdesc_t desc;
	desc.directed = true;
	desc.strict = true;
	desc.maingraph = true;
	desc.has_attrs = true;

	GVC_t* gvc = gvContext();
	std::set<const LibSWBF2::Wrappers::EntityClass*> entityClasses;

	// 1. Get all root class names
	for (LibSWBF2::SWBF2Handle h : lvls)
	{
		auto* level = con->GetLevel(h);
		const LibSWBF2::Types::List<LibSWBF2::Wrappers::EntityClass>& classes = level->GetEntityClasses();

		for (int i = 0; i < classes.Size(); ++i)
		{
			CrawlNodesRecursive(classes[i]);
			entityClasses.emplace(&classes[i]);
		}
	}

	if (customClasses.size() > 0)
	{
		RootClasses.clear();
		std::copy(customClasses.begin(), customClasses.end(), std::inserter(RootClasses, RootClasses.end()));
	}

	if (!fs::exists("PlotOut"))
	{
		fs::create_directory("PlotOut");
	}

	// 2. Create one graph per root class
	for (const std::string& root : RootClasses)
	{
		Agraph_t* graph = agopen((char*)root.c_str(), desc, nullptr);

		agattr(graph, AGRAPH, (char*)"dpi", (char*)"320.0/0.0");
		agset(graph, (char*)"dpi", (char*)"320.0/0.0");
		agattr(graph, AGNODE, (char*)"shape", (char*)"box");

		std::map<std::string, Agnode_t*> classNodes;

		for (const auto ec : entityClasses)
		{
			AddNodeRecursive(graph, classNodes, root, *ec);
		}

		gvLayout(gvc, graph, "dot");
		//gvRenderFilename(gvc, graph, "svg", fmt::format("PlotOut/{0}.svg", root).c_str());
		gvRenderFilename(gvc, graph, "png", fmt::format("PlotOut/{0}.png", root).c_str());
		//gvRenderFilename(gvc, graph, "dot", fmt::format("PlotOut/{0}.dot", root).c_str());
		gvFreeLayout(gvc, graph);

		agclose(graph);
	}

	LibSWBF2::Container::Delete(con);
	con = nullptr;

	gvFreeContext(gvc);
	gvc = nullptr;

	std::ofstream mcFile;
	mcFile.open("SWBF2RootClasses.txt", std::ios::out);
	for (const std::string& line : RootClasses)
	{
		mcFile << line << '\n';
	}
	mcFile.close();

    return 0;
}