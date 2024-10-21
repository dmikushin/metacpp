#include "ScraperTool.hpp"

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/Tooling.h>

#include "ASTScraper.hpp"

using namespace clang;
using namespace clang::tooling;

namespace metacpp {
	ScraperTool::ScraperTool(std::string source, std::vector<std::string> flags) {
#if _DEBUG
		// For now
		flags.push_back("-D_DEBUG");
#endif
		flags.push_back("-xc++");
        flags.push_back("-std=c++20");
        flags.push_back("-Wno-invalid-constexpr");

		m_CompilationDatabase = new clang::tooling::FixedCompilationDatabase(".", flags);
		m_ClangTool = new clang::tooling::ClangTool(*m_CompilationDatabase, source);
	}

	ScraperTool::~ScraperTool() {
		delete m_CompilationDatabase;
		delete m_ClangTool;
	}

	bool ScraperTool::Run(ASTScraper* scraper) {
		// Consumer
		class ASTScraperConsumer : public clang::ASTConsumer {
		public:
			ASTScraperConsumer(ASTScraper* scraper) : scraper(scraper) {};

			void HandleTranslationUnit(clang::ASTContext& context) {
				scraper->ScrapeTranslationUnit(context.getTranslationUnitDecl());
			}

		private:
			ASTScraper* scraper;
		};

		// Action
		class ASTScraperAction : public clang::ASTFrontendAction {
		public:
			ASTScraperAction(ASTScraper* scraper) : scraper(scraper) {};

			std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance& Compiler, llvm::StringRef InFile) override {
                (void)InFile;
				scraper->SetContext(&Compiler.getASTContext());
				return std::unique_ptr<clang::ASTConsumer>(new ASTScraperConsumer(scraper));
			};
		private:
			ASTScraper* scraper;
		};

		// Factory
		class ActionFactory : public clang::tooling::FrontendActionFactory {
		public:
			ActionFactory(ASTScraper* scraper) : scraper(scraper) {};

			std::unique_ptr<FrontendAction> create() override { return std::make_unique<ASTScraperAction>(scraper); }

		private:
			ASTScraper* scraper;
		};
		auto scraperActionFactory = std::unique_ptr<ActionFactory>(new ActionFactory(scraper));

		return m_ClangTool->run(scraperActionFactory.get()) == 0;
	}
}
