#include "ASTScraper.hpp"

#include <iostream>

#include <clang/AST/ASTContext.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/RecordLayout.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/VTableBuilder.h>
#include <clang/AST/Decl.h>
#include <clang/Basic/TargetInfo.h>

#include "MetaCPP/Type.hpp"

namespace metacpp {
	ASTScraper::ASTScraper(Storage* storage, const Configuration& config)
			: m_Config(config), m_Storage(storage) {
	}

	void ASTScraper::ScrapeTranslationUnit(const clang::TranslationUnitDecl* tuDecl) {
		ScrapeDeclContext(tuDecl, nullptr);
	}

	void ASTScraper::ScrapeDeclContext(const clang::DeclContext* ctxDecl, Type* parent) {
		for (clang::DeclContext::decl_iterator it = ctxDecl->decls_begin(); it != ctxDecl->decls_end(); ++it) {
			const clang::Decl* decl = *it;
			if (decl->isInvalidDecl())
				continue;

			auto namedDecl = clang::dyn_cast<clang::NamedDecl>(decl);
			if (namedDecl)
				ScrapeNamedDecl(namedDecl, parent);
		}
	}

	void ASTScraper::ScrapeNamedDecl(const clang::NamedDecl* namedDecl, Type* parent) {
		clang::Decl::Kind kind = namedDecl->getKind();
		switch (kind) {
			case clang::Decl::Kind::ClassTemplate:
			case clang::Decl::Kind::CXXConstructor:
			case clang::Decl::Kind::CXXDestructor:
			case clang::Decl::Kind::CXXMethod:
			default: {
				// Unhandled
				//std::string qualifiedName = namedDecl->getQualifiedNameAsString();
				//std::cout << "Unhandled! " << namedDecl->getDeclKindName() << ": " << qualifiedName << std::endl;
				break;
			}
			case clang::Decl::Kind::Namespace:
				ScrapeDeclContext(namedDecl->castToDeclContext(namedDecl), parent);
				break;
			case clang::Decl::Kind::CXXRecord:
				ScrapeCXXRecordDecl(clang::dyn_cast<clang::CXXRecordDecl>(namedDecl), parent);
				break;
			case clang::Decl::Kind::Field:
				ScrapeFieldDecl(clang::dyn_cast<clang::FieldDecl>(namedDecl), parent);
				break;
		}
	}

	Type* ASTScraper::ScrapeCXXRecordDecl(const clang::CXXRecordDecl* cxxRecordDecl, Type* parent) {
		if (cxxRecordDecl->isAnonymousStructOrUnion()) {
			ScrapeDeclContext(cxxRecordDecl, parent);
			return 0;
		}
		int access = 0;
		const clang::Type* cType = cxxRecordDecl->getTypeForDecl();
		metacpp::Type* type = ScrapeType(cType, 1);

		if (type) {
			const clang::CXXRecordDecl* typeCxxRecordDecl = cType->getAsCXXRecordDecl();

			access = std::max(access, (int)TransformAccess(cxxRecordDecl));
			auto context = cxxRecordDecl->getDeclContext();
			while(context) {
				if(auto record = clang::dyn_cast<clang::CXXRecordDecl>(context)) {
					access = std::max(access, (int)TransformAccess(record));
				}
				context = context->getParent();
			}
			for(const auto& arg : type->GetTemplateArguments()) {
				if(std::holds_alternative<QualifiedType>(arg)) {
					auto argType = m_Storage->GetType(std::get<QualifiedType>(arg).GetTypeID());
					if(argType) {
						access = std::max(access, (int)argType->GetAccess());
					}
				}
			}
			type->SetAccess((AccessSpecifier)access);
			type->SetHasDefaultConstructor(!typeCxxRecordDecl->hasUserProvidedDefaultConstructor() && typeCxxRecordDecl->needsImplicitDefaultConstructor());
			type->SetHasDefaultDestructor(typeCxxRecordDecl->needsImplicitDestructor());

			// methods
			for (auto it = cxxRecordDecl->method_begin(); it != cxxRecordDecl->method_end(); it++) {
				clang::CXXMethodDecl* method = *it;
				if (method != 0) {
					ScrapeMethodDecl(method, type);
				}
			}

			if (typeCxxRecordDecl->isAbstract()) {
				type->SetHasDefaultConstructor(false);
				type->SetHasDefaultDestructor(false);
			}
		}

		return type;
	}

	std::vector<TemplateArgument>
	ASTScraper::ResolveCXXRecordTemplate(const clang::CXXRecordDecl* cxxRecordDecl, QualifiedName& qualifiedName) {
		std::string typeName = cxxRecordDecl->getQualifiedNameAsString();
		std::vector<TemplateArgument> templateArgs;

		// Template arguments
		auto templateDecl = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(cxxRecordDecl);
		if (templateDecl) {
			typeName += "<";

			const clang::TemplateArgumentList& args = templateDecl->getTemplateArgs();
			bool first = true;
			for (int i = 0, end = (int) args.size(); i < end; i++) {
				const clang::TemplateArgument& arg = args[i];
				ResolveCXXRecordTemplateArgument(typeName, templateArgs, first, arg);
			}

			typeName += ">";
		}

		qualifiedName = ResolveQualifiedName(typeName);

		return templateArgs;
	}
	void ASTScraper::ResolveCXXRecordTemplateArgument(std::string& typeName, std::vector<TemplateArgument>& templateArgs, bool& first, const clang::TemplateArgument& arg)
	{

		TemplateArgument argument;
		switch (arg.getKind()) {
			case clang::TemplateArgument::Type:
				argument = ResolveQualType(arg.getAsType(), 1);
				break;
			case clang::TemplateArgument::Integral:
				argument = arg.getAsIntegral().getLimitedValue();
				break;
			case clang::TemplateArgument::Pack: {
				auto nestedArgs = arg.getPackAsArray();
				for(auto nestedArg : nestedArgs) {
					ResolveCXXRecordTemplateArgument(typeName, templateArgs, first, nestedArg);
				}
				return;
			}
				break;
			default:
				std::cout << "Unsupported template argument!" << std::endl;
				break;
		}

		if (!first)
			typeName += ",";

		if (std::holds_alternative<QualifiedType>(argument)) {
			QualifiedType qualifiedType = std::get<QualifiedType>(argument);
			if (qualifiedType.GetTypeID() != 0)
				typeName += qualifiedType.GetQualifiedName(m_Storage);
			else
				typeName += "INVALID";
		} else {
			unsigned long long integral = std::get<unsigned long long>(argument);
			typeName += std::to_string(integral);
		}
		first = false;

		templateArgs.push_back(argument);
	}

	Type* ASTScraper::ScrapeType(const clang::Type* cType, size_t arraySize) {
		QualifiedName qualifiedName;
		metacpp::TypeKind kind;
		std::vector<TemplateArgument> templateArgs;

		switch (cType->getTypeClass()) {
			case clang::Type::TypeClass::Builtin: {
				static clang::LangOptions lang_opts;
				static clang::PrintingPolicy printing_policy(lang_opts);

				auto builtin = cType->getAs<clang::BuiltinType>();
				std::string name = builtin->getName(printing_policy).str();
				if (name == "_Bool")
					name = "bool";
				qualifiedName = QualifiedName({}, name, "", arraySize != 1 ? std::to_string(arraySize) : "");
				kind = metacpp::TypeKind::PRIMITIVE;
				break;
			}
			case clang::Type::TypeClass::Record: {
				auto cxxRecordDecl = cType->getAsCXXRecordDecl();
				if (cxxRecordDecl->isThisDeclarationADefinition() == clang::VarDecl::DeclarationOnly) {
					if (cxxRecordDecl->getDeclKind() != clang::Decl::ClassTemplateSpecialization) {
						return 0; // forward declaration
					}
				}
				templateArgs = ResolveCXXRecordTemplate(cxxRecordDecl, qualifiedName);

				if (qualifiedName.GetName().size() == 0) {
					return 0;
				}

				kind = cType->isStructureType() ? metacpp::TypeKind::STRUCT : metacpp::TypeKind::CLASS;
				break;
			}
            case clang::Type::TypeClass::FunctionProto:
            case clang::Type::TypeClass::Vector:
            case clang::Type::TypeClass::Pointer:
            case clang::Type::TypeClass::InjectedClassName:
            case clang::Type::TypeClass::MemberPointer:
            case clang::Type::TypeClass::Enum:
//				std::cout << "[Ignored TypeClass: " << cType->getTypeClassName() << "]" << std::endl;
//              cType->dump();
				return 0;
			case clang::Type::TypeClass::ConstantArray: {
				if(arraySize == 1) {
					if (auto arrayType = clang::dyn_cast<clang::ConstantArrayType>(cType)) {
						auto elemType = arrayType->getElementType();
						auto elemArraySize = arrayType->getSize().getLimitedValue();
						auto typeId = ResolveQualType(elemType, elemArraySize).GetTypeID();
						if (m_Storage->HasType(typeId)) {
							return m_Storage->GetType(typeId);
						}
					}
				}
				std::cout << "[Failed to scrape ConstantArray]" << std::endl;
				return 0;
			}
			default:
				// TODO: this should never happen: we print it because we are afraid that it probably should be supported.
				std::cout << "[Unsupported TypeClass: " << cType->getTypeClassName() << "]" << std::endl;
//                cType->dump();
				return 0;
		}

		TypeID typeId = m_Storage->AssignTypeID(qualifiedName.FullQualified());

		if (m_Storage->HasType(typeId))
			return m_Storage->GetType(typeId);

		metacpp::Type* type = new metacpp::Type(typeId, qualifiedName);
		m_Storage->AddType(type);

		type->SetArraySize(arraySize);
		type->SetKind(kind);
		type->SetHasDefaultConstructor(cType->getTypeClass() == clang::Type::TypeClass::Builtin && qualifiedName.GetName() != "void");
		type->SetHasDefaultDestructor(cType->getTypeClass() == clang::Type::TypeClass::Builtin && qualifiedName.GetName() != "void");
		type->SetPolymorphic(false);

		if (auto cxxRecordDecl = cType->getAsCXXRecordDecl()) {
			if (cxxRecordDecl->getDeclKind() == clang::Decl::ClassTemplateSpecialization
			    && cxxRecordDecl->isThisDeclarationADefinition() == clang::VarDecl::DeclarationOnly) {
				type->SetPolymorphic(false);
			} else {
				type->SetPolymorphic(cxxRecordDecl->isPolymorphic());

				// Parse base classes
				for (auto it = cxxRecordDecl->bases_begin(); it != cxxRecordDecl->bases_end(); it++) {
					QualifiedType base_qtype = ResolveQualType(it->getType(), 1);
					type->AddBaseType(base_qtype, TransformAccess(it->getAccessSpecifier()));

					if (it == cxxRecordDecl->bases_begin()) {
						m_Storage->GetType(base_qtype.GetTypeID())->AddDerivedType(typeId);
					}
				}

				// Scrape annotations
				std::vector<std::string> annotations = ScrapeAnnotations(cxxRecordDecl);
			}
		}

		for (const TemplateArgument& argument : templateArgs)
			type->AddTemplateArgument(argument);

		if (auto declCtx = cType->getAsTagDecl()->castToDeclContext(cType->getAsTagDecl()))
			ScrapeDeclContext(declCtx, type);

		return type;
	}

	void ASTScraper::ScrapeFieldDecl(const clang::FieldDecl* fieldDecl, Type* parent) {
		if (!parent)
			return;
		if (fieldDecl->isAnonymousStructOrUnion())
			return;

		metacpp::QualifiedName qualifiedName = ResolveQualifiedName(fieldDecl->getQualifiedNameAsString());

		Field field(ResolveQualType(fieldDecl->getType(), 1), qualifiedName);

		field.SetOffset(m_Context->getFieldOffset(fieldDecl) / 8);

		parent->AddField(field);
	}

	void ASTScraper::ScrapeMethodDecl(const clang::CXXMethodDecl* cxxMethodDecl, Type* parent) {
		auto constructor = clang::dyn_cast<clang::CXXConstructorDecl>(cxxMethodDecl);
		auto destructor = clang::dyn_cast<clang::CXXDestructorDecl>(cxxMethodDecl);

		if (constructor && !constructor->isDeleted() && constructor->isDefaultConstructor()
		    && constructor->getAccess() == clang::AccessSpecifier::AS_public
		    && parent->GetQualifiedName().GetName() != "__cow_string") {
			parent->SetHasDefaultConstructor(true);
		}
		if (destructor && !destructor->isDeleted()
		    && destructor->getAccess() == clang::AccessSpecifier::AS_public
		    && parent->GetQualifiedName().GetName() != "__cow_string") {
			parent->SetHasDefaultDestructor(true);
		}

		metacpp::QualifiedName qualifiedName = ResolveQualifiedName(cxxMethodDecl->getQualifiedNameAsString());

		Method method(qualifiedName);
		method.SetOwner(parent->GetTypeID());

		//cxxMethodDecl->dumpColor();

		//std::cout << cxxMethodDecl->isUserProvided() << cxxMethodDecl->isUsualDeallocationFunction() << cxxMethodDecl->isCopyAssignmentOperator() << cxxMethodDecl->isMoveAssignmentOperator() << std::endl;
		//std::cout << cxxMethodDecl->isDefaulted() << std::endl;

		// params
		for (auto it = cxxMethodDecl->param_begin(); it != cxxMethodDecl->param_end(); it++) {
			clang::ParmVarDecl* param = *it;
			std::string name = param->getName().str();
			QualifiedType qtype = ResolveQualType(param->getType(), 1);

			MethodParameter methodParam(name, qtype);
			method.AddParameter(methodParam);
		}

		parent->AddMethod(method);
	}

	QualifiedType ASTScraper::ResolveQualType(clang::QualType qualType, size_t arraySize) {
		MakeCanonical(qualType);

		QualifiedType qualifiedType;

		qualifiedType.SetConst(qualType.isConstant(*m_Context));
		qualifiedType.SetArraySize(arraySize);
		if (auto ptr = clang::dyn_cast<clang::PointerType>(qualType.split().Ty)) {
			qualifiedType.SetQualifierOperator(QualifierOperator::POINTER);
			qualType = ptr->getPointeeType();
		} else if (auto arr = clang::dyn_cast<clang::ConstantArrayType>(qualType.split().Ty)) {
			qualifiedType.SetArraySize(arr->getSize().getLimitedValue());
			qualifiedType.SetQualifierOperator(QualifierOperator::ARRAY);
			qualType = arr->getElementType();
		} else if (auto ref = clang::dyn_cast<clang::ReferenceType>(qualType.split().Ty)) {
			qualifiedType.SetQualifierOperator(QualifierOperator::REFERENCE);
			qualType = ref->getPointeeType();
		} else
			qualifiedType.SetQualifierOperator(QualifierOperator::VALUE);

		MakeCanonical(qualType);

		const clang::Type* cType = qualType.split().Ty;
		Type* type = ScrapeType(cType, arraySize);
		qualifiedType.SetTypeID(type ? type->GetTypeID() : 0);

		return qualifiedType;
	}

	void ASTScraper::MakeCanonical(clang::QualType& qualType) {
		if (!qualType.isCanonical())
			qualType = qualType.getCanonicalType();
	}

	QualifiedName ASTScraper::ResolveQualifiedName(std::string qualifiedName) {
		RemoveAll(qualifiedName, "::(anonymous union)");
		RemoveAll(qualifiedName, "::(anonymous struct)");
		RemoveAll(qualifiedName, "::(anonymous)");

		RemoveAll(qualifiedName, "(anonymous union)");
		RemoveAll(qualifiedName, "(anonymous struct)");
		RemoveAll(qualifiedName, "(anonymous)");

		return QualifiedName(qualifiedName);
	}

	void ASTScraper::RemoveAll(std::string& source, const std::string& search) {
		std::string::size_type n = search.length();
		for (std::string::size_type i = source.find(search); i != std::string::npos; i = source.find(search))
			source.erase(i, n);
	}

	AccessSpecifier ASTScraper::TransformAccess(const clang::AccessSpecifier as) {
		switch (as) {
			case clang::AccessSpecifier::AS_public:
			case clang::AccessSpecifier::AS_none:
				return AccessSpecifier::PUBLIC;
			case clang::AccessSpecifier::AS_protected:
				return AccessSpecifier::PROTECTED;
			case clang::AccessSpecifier::AS_private:
			default:
				return AccessSpecifier::PRIVATE;
		}
	}

	std::vector<std::string> ASTScraper::ScrapeAnnotations(const clang::Decl* decl) {
		std::vector<std::string> annotations;

		if (decl && decl->hasAttrs()) {
			auto it = decl->specific_attr_begin<clang::AnnotateAttr>();
			auto itEnd = decl->specific_attr_end<clang::AnnotateAttr>();

			// __attribute__((annotate("...")))
			for (; it != itEnd; it++) {
				const clang::AnnotateAttr* attr = clang::dyn_cast<clang::AnnotateAttr>(*it);
				if (attr)
					annotations.emplace_back(attr->getAnnotation());
			}
		}

		return annotations;
	}

	bool ASTScraper::IsReflected(const std::vector<std::string>& attrs) {
		if (m_Config.AnnotationRequired.size() == 0)
			return true;
		return std::find(attrs.begin(), attrs.end(), m_Config.AnnotationRequired) != attrs.end();
	}

	void ASTScraper::SetContext(clang::ASTContext* context) {
		m_Context = context;
	}

	AccessSpecifier ASTScraper::TransformAccess(const clang::CXXRecordDecl* decl)
	{
		switch (decl->getAccess()) {
		case clang::AccessSpecifier::AS_none:
			return decl->isClass() ? AccessSpecifier::PRIVATE : AccessSpecifier::PUBLIC;
		case clang::AccessSpecifier::AS_public:
			return AccessSpecifier::PUBLIC;
		case clang::AccessSpecifier::AS_protected:
			return AccessSpecifier::PROTECTED;
		case clang::AccessSpecifier::AS_private:
		default:
			return AccessSpecifier::PRIVATE;
		}
	}
}
