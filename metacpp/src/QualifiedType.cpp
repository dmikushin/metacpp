#include "MetaCPP/QualifiedType.hpp"

#include "MetaCPP/Storage.hpp"
#include "MetaCPP/Type.hpp"

namespace metacpp {
	QualifiedType::QualifiedType()
			: m_Operator(QualifierOperator::VALUE), m_Const(false), m_Type(0), m_ArraySize(1) {
	}

	QualifiedType::QualifiedType(TypeID typeID, QualifierOperator qualifierOperator, bool is_const, size_t arraySize)
			: m_Operator(qualifierOperator), m_Const(is_const), m_Type(typeID), m_ArraySize(arraySize) {
	}

	std::string QualifiedType::GetQualifiedName(const Storage* storage) const {
		Type* type = storage->GetType(m_Type);

		std::string result = "";
		if (m_Const)
			result += "const ";
		if (type)
			result += type->GetQualifiedName().FullQualified();
		else
			result += "UNKNOWN";

		switch (m_Operator) {
			case QualifierOperator::POINTER:
				result += "*";
				break;
			case QualifierOperator::REFERENCE:
				result += "&";
				break;
			case QualifierOperator::VALUE:
				break;
			case QualifierOperator::ARRAY:
				break;
		}

		return result;
	}

	void QualifiedType::SetTypeID(const TypeID typeID) {
		m_Type = typeID;
	}

	void QualifiedType::SetQualifierOperator(const QualifierOperator qualifierOperator) {
		m_Operator = qualifierOperator;
	}

	void QualifiedType::SetConst(const bool is_const) {
		m_Const = is_const;
	}

	void QualifiedType::SetArraySize(const std::size_t arraySize) {
		m_ArraySize = arraySize;
	}

	TypeID QualifiedType::GetTypeID() const {
		return m_Type;
	}

	QualifierOperator QualifiedType::GetQualifierOperator() const {
		return m_Operator;
	}

	bool QualifiedType::IsConst() const {
		return m_Const;
	}

	std::size_t QualifiedType::GetArraySize() const {
		return m_ArraySize;
	}
}
