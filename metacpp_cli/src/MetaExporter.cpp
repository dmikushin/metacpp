#include "MetaExporter.hpp"

#include <iostream>
#include <fstream>
#include <string>

#include "Templates.hpp"

namespace metacpp {
	MetaExporter::MetaExporter(const Storage* storage)
			: m_Storage(storage) {
	}

	void MetaExporter::Export(const std::string& inputSource, const std::string& outputHeader, const std::string& outputSource) {

		mustache::data storageData = ExportStorage();

		std::string new_source = GenerateSourceFile({ outputHeader }, storageData);
		std::string new_header = GenerateHeaderFile({ inputSource }, storageData);

        std::string old_source = ReadFileContent(outputSource);
        std::string old_header = ReadFileContent(outputHeader);

        if(old_source != new_source) {
            std::ofstream out_source(outputSource);
            out_source << new_source;
        }

        if(old_header != new_header) {
            std::ofstream out_header(outputHeader);
            out_header << new_header;
        }
	}

	std::string MetaExporter::GenerateSourceFile(const std::vector<std::string>& includes, mustache::data& storageData) {
		mustache::mustache source_template(templates::source);
		source_template.set_custom_escape([](const std::string& str) { return str; });

		SetIncludes(includes, storageData);

		return source_template.render(storageData);
	}

	std::string MetaExporter::GenerateHeaderFile(const std::vector<std::string>& includes, mustache::data& storageData) {
		mustache::mustache header_template(templates::header);
		header_template.set_custom_escape([](const std::string& str) { return str; });

		SetIncludes(includes, storageData);

		return header_template.render(storageData);
	}

	void MetaExporter::SetIncludes(const std::vector<std::string>& includes, mustache::data& data) {
		mustache::data inclds{ mustache::data::type::list };
		for (const std::string& file : includes)
			inclds << mustache::data{ "file", file };
		data["includes"] = mustache::data{ inclds };
	}

	mustache::data MetaExporter::ExportStorage() {
		mustache::data data;

		// IDs
		mustache::data ids{ mustache::data::type::list };
		for (const auto& kv : m_Storage->m_IDs) {
			mustache::data entry;
			entry["id"] = std::to_string(kv.second);
			entry["qualifiedName"] = kv.first;
			ids << entry;
		}
		data["ids"] = mustache::data{ ids };

		// Types
		mustache::data types{ mustache::data::type::list };
		for (const auto& kv : m_Storage->m_Types) {
			if(!(
				kv.second->GetQualifiedName().GetName() == "__va_list_tag"))
				types << ExportType(kv.second);
		}
		data["types"] = mustache::data{ types };

		return data;
	}

	mustache::data MetaExporter::ExportType(const Type* type) {
		mustache::data data;

		data["id"] = std::to_string(type->m_ID);
		data["qualifiedName"] = type->m_QualifiedName.FullQualified();
		data["name"] = type->m_QualifiedName.GetName();
		data["arraySize"] = std::to_string(type->m_ArraySize);
		data["kind"] = std::to_string(static_cast<int>(type->m_Kind));
		data["access"] = std::to_string(static_cast<int>(type->m_Access));
		bool valid = type->IsValid();
		data["polymorphic"] = std::to_string(type->m_Polymorphic);
		data["hasDefaultConstructor"] = std::to_string(type->m_HasDefaultConstructor);
		data["hasDefaultDestructor"] = std::to_string(type->m_HasDefaultDestructor);
		data["qualifiedMemberName"] = type->m_QualifiedName.MemberQualified("value");

		bool seqContainer = type->IsSequentialContainer();
		bool assocContainer = type->IsAssociativeContainer();

		if (type->m_TemplateArguments.size() > 0) {
			auto argument = type->m_TemplateArguments[0];
			if (std::holds_alternative<QualifiedType>(argument))
				data["containerValueQualifiedName"] = std::get<QualifiedType>(argument).GetQualifiedName(m_Storage);
		}

		data["isSequentialContainer"] = std::to_string(seqContainer);
		data["isStaticArray"] = std::to_string(type->IsStaticArray());
		data["isAssociativeContainer"] = std::to_string(assocContainer);
		data["isContainer"] = std::to_string(seqContainer || assocContainer);

		// Base Types
		mustache::data baseTypes{ mustache::data::type::list };
		for (const BaseType& base : type->m_BaseTypes) {
			mustache::data baseType;
			baseType["qualifiedType"] = ExportQualifiedType(base.type);
			baseType["access"] = std::to_string(static_cast<int>(base.access));
			baseTypes << baseType;
		}

		// Derived Types
		mustache::data derivedTypes{ mustache::data::type::list };
		for (const TypeID derived_typeid : type->m_DerivedTypes) {
			Type* derived_type = m_Storage->GetType(derived_typeid);
			if (!derived_type->IsValid())
				continue;

			mustache::data data;
			data["derivedTypeId"] = std::to_string(derived_typeid);
			data["derivedQualifiedName"] = derived_type->GetQualifiedName().FullQualified();
			derivedTypes << data;
		}

		// Template Arguments
		mustache::data templateArguments{ mustache::data::type::list };
		for (const TemplateArgument& arg : type->m_TemplateArguments) {
			if (std::holds_alternative<QualifiedType>(arg)) {
				const QualifiedType& qtype = std::get<QualifiedType>(arg);
				Type* type = m_Storage->GetType(qtype.GetTypeID());
				if(type)
					valid &= type->IsValid();
			}
			templateArguments << ExportTemplateArgument(arg);
		}

		// Fields
		mustache::data fields{ mustache::data::type::list };
		for (const Field& field : type->m_Fields)
			fields << ExportField(field);

		// Methods
		mustache::data methods{ mustache::data::type::list };
		for (const Method& method : type->m_Methods)
			methods << ExportMethod(method);

		data["fields"] = mustache::data{ fields };
		data["methods"] = mustache::data{ methods };
		data["baseTypes"] = mustache::data{ baseTypes };
		data["derivedTypes"] = mustache::data{ derivedTypes };
		data["templateArguments"] = mustache::data{ templateArguments };
		data["valid"] = std::to_string(valid);
		data["hasSize"] = std::to_string(valid && type->HasSize());

		return data;
	}

	mustache::data MetaExporter::ExportQualifiedType(const QualifiedType& qtype) {
		mustache::data data;
		data["typeID"] = std::to_string(qtype.m_Type);
		data["const"] = std::to_string(qtype.m_Const);
		data["operator"] = std::to_string(static_cast<int>(qtype.m_Operator));
		data["arraySize"] = std::to_string(qtype.m_ArraySize);
		return data;
	}

	mustache::data MetaExporter::ExportField(const Field& field) {
		mustache::data data;
		data["ownerId"] = std::to_string(field.m_Owner);
		data["qualifiedType"] = ExportQualifiedType(field.m_Type);
		data["qualifiedName"] = field.m_QualifiedName.FullQualified();
		data["name"] = field.m_QualifiedName.GetName();
		data["offset"] = std::to_string(field.m_OffsetInBytes);
		return data;
	}

	mustache::data MetaExporter::ExportTemplateArgument(const TemplateArgument& argument) {
		mustache::data data;

		if (std::holds_alternative<QualifiedType>(argument)) {
			data["isIntegral"] = std::to_string(false);
			data["integralValue"] = std::to_string(0);
			const QualifiedType& qtype = std::get<QualifiedType>(argument);
			data["typeID"] = std::to_string(qtype.m_Type);
			data["const"] = std::to_string(qtype.m_Const);
			data["operator"] = std::to_string(static_cast<int>(qtype.m_Operator));
			data["arraySize"] = std::to_string(static_cast<int>(qtype.m_ArraySize));
		} else {
			data["isIntegral"] = std::to_string(true);
			data["integralValue"] = std::to_string(std::get<unsigned long long>(argument));
			data["typeID"] = std::to_string(0);
			data["const"] = std::to_string(false);
			data["operator"] = std::to_string(0);
			data["arraySize"] = std::to_string(0);
		}
		return data;
	}

	mustache::data MetaExporter::ExportMethod(const Method& method) {
		mustache::data data;
		data["qualifiedName"] = method.m_QualifiedName.FullQualified();

		mustache::data params{ mustache::data::type::list };
		for (const MethodParameter& parameter : method.m_Parameters)
			params << ExportMethodParameter(parameter);

		data["methodParams"] = mustache::data{ params };
		return data;
	}

	mustache::data MetaExporter::ExportMethodParameter(const MethodParameter& parameter) {
		mustache::data data;
		data["methodParamName"] = parameter.GetName();
		data["qualifiedType"] = ExportQualifiedType(parameter.GetType());
		return data;
	}

    std::string MetaExporter::ReadFileContent(const std::string &filename) {
        std::ifstream in(filename, std::ios::in | std::ios::binary);
        if (in)
        {
            std::string contents;
            in.seekg(0, std::ios::end);
            contents.resize(in.tellg());
            in.seekg(0, std::ios::beg);
            in.read(&contents[0], contents.size());
            in.close();
            return(contents);
        }
        return "";
    }
}
