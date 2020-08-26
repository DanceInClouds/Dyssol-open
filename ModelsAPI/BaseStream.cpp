/* Copyright (c) 2020, Dyssol Development Team. All rights reserved. This file is part of Dyssol. See LICENSE file for license information. */

#include "Stream.h"
#include "TimeDependentValue.h"
#include "Phase.h"
#include "ContainerFunctions.h"
#include "DistributionsGrid.h"
#include "DyssolStringConstants.h"
#include "DyssolUtilities.h"

// TODO: remove all reinterpret_cast and static_cast for MDMatrix.
// TODO: remove AddTimePoint() whet it is not needed by MDMatrix.

CBaseStream::CBaseStream(const std::string& _key)
{
	m_key = _key.empty() ? StringFunctions::GenerateRandomKey() : _key;
	m_overall.insert({ EOverall::OVERALL_MASS, std::make_unique<CTimeDependentValue>() });
}

CBaseStream::CBaseStream(const CBaseStream& _other) :
	m_timePoints{ _other.m_timePoints },
	m_compounds{ _other.m_compounds },
	m_cacheSettings{ _other.m_cacheSettings },
	m_materialsDB{ _other.m_materialsDB },
	m_grid{ _other.m_grid }
{
	m_key = StringFunctions::GenerateRandomKey();
	for (const auto& [type, param] : _other.m_overall)
		m_overall.insert({ type, std::make_unique<CTimeDependentValue>(*param) });
	for (const auto& [state, phase] : _other.m_phases)
		m_phases.insert({ state, std::make_unique<CPhase>(*phase) });
}

void CBaseStream::Clear()
{
	m_timePoints.clear();
	m_overall.clear();
	m_phases.clear();
	m_compounds.clear();
	m_lookupTables.Clear();
}

void CBaseStream::SetupStructure(const CBaseStream& _other)
{
	Clear();
	m_materialsDB = _other.m_materialsDB;
	m_grid = _other.m_grid;
	m_compounds = _other.m_compounds;
	for (const auto& [type, old] : _other.m_overall)
	{
		auto* overall = AddOverallProperty(type);
		overall->SetName(old->GetName());
		overall->SetUnits(old->GetUnits());
	}
	for (const auto& [type, old] : _other.m_phases)
	{
		auto* phase = AddPhase(type);
		phase->SetName(old->GetName());
		phase->MDDistr()->SetMinimalFraction(old->MDDistr()->GetMinimalFraction());
	}
	SetCacheSettings(_other.m_cacheSettings);
}

bool CBaseStream::HaveSameStructure(const CBaseStream& _stream1, const CBaseStream& _stream2)
{
	// check overall parameters
	if (MapKeys(_stream1.m_overall) != MapKeys(_stream2.m_overall)) return false;

	// check phases
	if (MapKeys(_stream1.m_phases) != MapKeys(_stream2.m_phases)) return false;

	// check solid phase
	if (_stream1.HasPhase(EPhase::SOLID))
	{
		if (_stream1.m_phases.at(EPhase::SOLID)->MDDistr()->GetDimensions() != _stream2.m_phases.at(EPhase::SOLID)->MDDistr()->GetDimensions()) return false;
		if (_stream1.m_phases.at(EPhase::SOLID)->MDDistr()->GetClasses() != _stream2.m_phases.at(EPhase::SOLID)->MDDistr()->GetClasses())    return false;
	}

	return true;
}

std::string CBaseStream::GetName() const
{
	return m_name;
}

void CBaseStream::SetName(const std::string& _name)
{
	m_name = _name;
}

std::string CBaseStream::GetKey() const
{
	return m_key;
}

void CBaseStream::SetKey(const std::string& _key)
{
	m_key = _key;
}

void CBaseStream::AddTimePoint(double _time)
{
	if (HasTime(_time)) return;
	CopyTimePoint(_time, GetPreviousTimePoint(_time));
}

void CBaseStream::CopyTimePoint(double _timeDst, double _timeSrc)
{
	if (_timeDst < 0) return;

	// insert time point
	InsertTimePoint(_timeDst);

	// copy data in overall parameters
	for (auto& [type, param] : m_overall)
		param->CopyTimePoint(_timeDst, _timeSrc);

	// copy data in phases
	for (auto& [state, phase] : m_phases)
		phase->CopyTimePoint(_timeDst, _timeSrc);
}

void CBaseStream::RemoveTimePoint(double _time)
{
	RemoveTimePoints(_time, _time);
}

void CBaseStream::RemoveTimePoints(double _timeBeg, double _timeEnd)
{
	if (m_timePoints.empty()) return;
	if (_timeBeg > _timeEnd) return;

	// remove time points
	const auto beg = std::lower_bound(m_timePoints.begin(), m_timePoints.end(), _timeBeg);
	const auto end = std::upper_bound(m_timePoints.begin(), m_timePoints.end(), _timeEnd);
	if (end == m_timePoints.begin()) return;
	m_timePoints.erase(beg, end);

	// remove data in overall parameters
	for (auto& [type, param] : m_overall)
		param->RemoveTimePoints(_timeBeg, _timeEnd);

	// remove data in phases
	for (auto& [state, phase] : m_phases)
		phase->RemoveTimePoints(_timeBeg, _timeEnd);
}

void CBaseStream::RemoveTimePointsAfter(double _time, bool _inclusive/* = false*/)
{
	const auto beg = _inclusive ? std::lower_bound(m_timePoints.begin(), m_timePoints.end(), _time) : std::upper_bound(m_timePoints.begin(), m_timePoints.end(), _time);
	if (beg == m_timePoints.end()) return;
	RemoveTimePoints(*beg, GetLastTimePoint());
}

void CBaseStream::ReduceTimePoints(double _timeBeg, double _timeEnd, double _step)
{
	std::vector<double> timePoints = GetTimePoints(_timeBeg, _timeEnd);
	if (timePoints.size() <= 3) return;
	timePoints.pop_back();

	size_t iTime1 = 0;
	size_t iTime2 = 1;
	while (iTime1 < timePoints.size() && iTime2 < timePoints.size())
	{
		if (std::fabs(timePoints[iTime1] - timePoints[iTime2]) < _step)
		{
			RemoveTimePoint(timePoints[iTime2]);
			VectorDelete(timePoints, iTime2);
		}
		else
		{
			iTime1++;
			iTime2++;
		}
	}
}

std::vector<double> CBaseStream::GetAllTimePoints() const
{
	return m_timePoints;
}

std::vector<double> CBaseStream::GetTimePoints(double _timeBeg, double _timeEnd) const
{
	if (m_timePoints.empty()) return {};
	if (_timeBeg > _timeEnd) return {};

	const auto end = std::upper_bound(m_timePoints.begin(), m_timePoints.end(), _timeEnd);
	if (end == m_timePoints.begin()) return {};
	const auto beg = std::lower_bound(m_timePoints.begin(), m_timePoints.end(), _timeBeg);

	return { beg, end };
}

std::vector<double> CBaseStream::GetTimePointsClosed(double _timeBeg, double _timeEnd) const
{
	std::vector<double> res = GetTimePoints(_timeBeg, _timeEnd);

	// no time points in the interval - return only limits
	if (res.empty())
		return { _timeBeg, _timeEnd };

	// add limits if necessary
	if (std::fabs(res.front() - _timeBeg) > m_epsilon)
		res.insert(res.begin(), _timeBeg);
	if (std::fabs(res.back() - _timeEnd) > m_epsilon)
		res.push_back(_timeEnd);

	return res;
}

double CBaseStream::GetLastTimePoint() const
{
	if (m_timePoints.empty()) return {};

	return m_timePoints.back();
}

double CBaseStream::GetPreviousTimePoint(double _time) const
{
	if (m_timePoints.empty()) return {};

	auto pos = std::lower_bound(m_timePoints.begin(), m_timePoints.end(), _time);
	if (pos == m_timePoints.begin()) return {};
	return *--pos;
}

CTimeDependentValue* CBaseStream::AddOverallProperty(EOverall _property)
{
	auto [it, flag] = m_overall.insert({ _property, std::make_unique<CTimeDependentValue>() });

	if (flag) // a new property added
	{
		// add time points
		// TODO: maybe remove this
		for (const auto& t : m_timePoints)
			it->second->AddTimePoint(t);
	}

	return it->second.get();
}

void CBaseStream::RemoveOverallProperty(EOverall _property)
{
	m_overall.erase(_property);
}

CTimeDependentValue* CBaseStream::GetOverallProperty(EOverall _property)
{
	if (!HasOverallProperty(_property)) return nullptr;

	return m_overall[_property].get();
}

const CTimeDependentValue* CBaseStream::GetOverallProperty(EOverall _property) const
{
	if (!HasOverallProperty(_property)) return nullptr;

	return m_overall.at(_property).get();
}

double CBaseStream::GetOverallProperty(double _time, EOverall _property) const
{
	if (!HasOverallProperty(_property))
	{
		if (_property == EOverall::OVERALL_TEMPERATURE)	return STANDARD_CONDITION_T;
		if (_property == EOverall::OVERALL_PRESSURE)	return STANDARD_CONDITION_P;
		return {};
	}

	return m_overall.at(_property)->GetValue(_time);
}

double CBaseStream::GetMass(double _time) const
{
	return GetOverallProperty(_time, EOverall::OVERALL_MASS);
}

double CBaseStream::GetTemperature(double _time) const
{
	return GetOverallProperty(_time, EOverall::OVERALL_TEMPERATURE);
}

double CBaseStream::GetPressure(double _time) const
{
	return GetOverallProperty(_time, EOverall::OVERALL_PRESSURE);
}

void CBaseStream::SetOverallProperty(double _time, EOverall _property, double _value)
{
	if (!HasOverallProperty(_property)) return;

	AddTimePoint(_time);
	m_overall[_property]->SetValue(_time, _value);
}

void CBaseStream::SetMass(double _time, double _value)
{
	SetOverallProperty(_time, EOverall::OVERALL_MASS, _value);
}

void CBaseStream::SetTemperature(double _time, double _value)
{
	SetOverallProperty(_time, EOverall::OVERALL_TEMPERATURE, _value);
}

void CBaseStream::SetPressure(double _time, double _value)
{
	SetOverallProperty(_time, EOverall::OVERALL_PRESSURE, _value);
}

void CBaseStream::AddCompound(const std::string& _compoundKey)
{
	if (HasCompound(_compoundKey)) return;

	m_compounds.push_back(_compoundKey);
	for (auto& [state, phase] : m_phases)
		phase->AddCompound(_compoundKey);

	m_lookupTables.Clear();
}

void CBaseStream::RemoveCompound(const std::string& _compoundKey)
{
	if (!HasCompound(_compoundKey)) return;

	VectorDelete(m_compounds, _compoundKey);
	for (auto& [state, phase] : m_phases)
		phase->RemoveCompound(_compoundKey);

	m_lookupTables.Clear();
}

void CBaseStream::ClearCompounds()
{
	const auto copy = m_compounds;
	for (const auto& c : copy)
		RemoveCompound(c);
}

std::vector<std::string> CBaseStream::GetAllCompounds() const
{
	return m_compounds;
}

double CBaseStream::GetCompoundFraction(double _time, const std::string& _compoundKey) const
{
	if (!HasCompound(_compoundKey)) return {};

	const size_t iCompound = CompoundIndex(_compoundKey);
	double res = 0.0;
	for (const auto& [state, phase] : m_phases)
		res += phase->GetFraction(_time) * phase->GetCompoundFraction(_time, iCompound);
	return res;
}

double CBaseStream::GetCompoundFraction(double _time, const std::string& _compoundKey, EPhase _phase) const
{
	if (!HasCompound(_compoundKey) || !HasPhase(_phase)) return {};

	return m_phases.at(_phase)->GetCompoundFraction(_time, CompoundIndex(_compoundKey));
}

double CBaseStream::GetCompoundFractionMoll(double _time, const std::string& _compoundKey, EPhase _phase) const
{
	if (!HasCompound(_compoundKey) || !HasPhase(_phase)) return {};

	const double molarMass = GetCompoundConstProperty(_compoundKey, MOLAR_MASS);
	if (molarMass == 0.0) return {};
	return m_phases.at(_phase)->GetCompoundFraction(_time, CompoundIndex(_compoundKey)) / molarMass * GetPhaseProperty(_time, _phase, MOLAR_MASS);
}

double CBaseStream::GetCompoundMass(double _time, const std::string& _compoundKey, EPhase _phase) const
{
	if (!HasCompound(_compoundKey) || !HasPhase(_phase)) return {};

	return m_phases.at(_phase)->GetFraction(_time)										// mass fraction of phase
		* m_phases.at(_phase)->GetCompoundFraction(_time, CompoundIndex(_compoundKey))	// mass fraction of compound in phase
		* GetMass(_time);																// whole mass
}

std::vector<double> CBaseStream::GetCompoundsFractions(double _time) const
{
	std::vector<double> res(m_compounds.size());
	for (size_t i = 0; i < m_compounds.size(); ++i)
		res[i] = GetCompoundFraction(_time, m_compounds[i]);
	return res;
}

std::vector<double> CBaseStream::GetCompoundsFractions(double _time, EPhase _phase) const
{
	if (!HasPhase(_phase)) return {};

	return m_phases.at(_phase)->GetCompoundsDistribution(_time);
}

void CBaseStream::SetCompoundFraction(double _time, const std::string& _compoundKey, EPhase _phase, double _value)
{
	if (!HasCompound(_compoundKey) || !HasPhase(_phase)) return;

	AddTimePoint(_time);
	m_phases[_phase]->SetCompoundFraction(_time, CompoundIndex(_compoundKey), _value);
}

void CBaseStream::SetCompoundsFractions(double _time, const std::vector<double>& _value)
{
	for (const auto& [state, phase] : m_phases)
		phase->SetCompoundsDistribution(_time, _value);
}

void CBaseStream::SetCompoundsFractions(double _time, EPhase _phase, const std::vector<double>& _value)
{
	if (!HasPhase(_phase)) return;

	m_phases[_phase]->SetCompoundsDistribution(_time, _value);
}

CPhase* CBaseStream::AddPhase(EPhase _phase)
{
	// add phase
	auto [it, flag] = m_phases.insert({ _phase, std::make_unique<CPhase>(_phase, *m_grid, m_compounds, m_cacheSettings) });

	if (flag) // a new phase added
	{
		// add time points
		// TODO: maybe remove this
		for (const auto& t : m_timePoints)
			it->second->AddTimePoint(t);
	}

	return it->second.get();
}

void CBaseStream::RemovePhase(EPhase _phase)
{
	if (!HasPhase(_phase)) return;

	m_phases.erase(_phase);
}

CPhase* CBaseStream::GetPhase(EPhase _phase)
{
	if (!HasPhase(_phase)) return {};

	return m_phases[_phase].get();
}

const CPhase* CBaseStream::GetPhase(EPhase _phase) const
{
	if (!HasPhase(_phase)) return {};

	return m_phases.at(_phase).get();
}

void CBaseStream::ClearPhases()
{
	for (const auto& p : MapKeys(m_phases))
		RemovePhase(p);
}

double CBaseStream::GetPhaseFraction(double _time, EPhase _phase) const
{
	if (!HasPhase(_phase)) return {};

	return m_phases.at(_phase)->GetFraction(_time);
}

double CBaseStream::GetPhaseMass(double _time, EPhase _phase) const
{
	if (!HasPhase(_phase)) return {};

	return GetMass(_time) * m_phases.at(_phase)->GetFraction(_time);
}

double CBaseStream::GetPhaseProperty(double _time, EPhase _phase, EOverall _property) const
{
	if (!HasPhase(_phase)) return {};

	if (_property == EOverall::OVERALL_MASS)
		return GetPhaseMass(_time, _phase);
	return GetOverallProperty(_time, _property);
}

double CBaseStream::GetPhaseProperty(double _time, EPhase _phase, ECompoundConstProperties _property) const
{
	if (!HasPhase(_phase)) return {};

	// TODO: implement other properties if needed
	if (_property == MOLAR_MASS)
	{
		double res = 0.0;
		for (const auto& c : m_compounds)
		{
			const double molarMass = GetCompoundConstProperty(c, MOLAR_MASS);
			if (molarMass != 0.0)
				res += GetCompoundFraction(_time, c, _phase) / molarMass;
		}
		if (res != 0.0)
			return 1.0 / res;
	}

	return {};
}

double CBaseStream::GetPhaseProperty(double _time, EPhase _phase, ECompoundTPProperties _property) const
{
	if (!HasPhase(_phase)) return {};

	double res = 0.0;
	const double T = GetPhaseProperty(_time, _phase, EOverall::OVERALL_TEMPERATURE);
	const double P = GetPhaseProperty(_time, _phase, EOverall::OVERALL_PRESSURE);

	switch (_property)
	{
	case VAPOR_PRESSURE:
	{
		std::vector<double> pressures;
		for (const auto& c : m_compounds)
			pressures.push_back(m_materialsDB->GetTPPropertyValue(c, _property, T, P));
		return VectorMin(pressures);
	}
	case VISCOSITY:
		switch (_phase)
		{
		case EPhase::LIQUID:
			for (const auto& c : m_compounds)
			{
				const double visco = m_materialsDB->GetTPPropertyValue(c, _property, T, P);
				if (visco != 0.0)
					res += GetCompoundFraction(_time, c, _phase) * std::log(visco);
			}
			if (res != 0.0)
				return std::exp(res);
			break;
		case EPhase::VAPOR:
		{
			double numerator = 0.0, denominator = 0.0;
			for (const auto& c : m_compounds)
			{
				const double visco = m_materialsDB->GetTPPropertyValue(c, _property, T, P);
				const double mollMass = GetCompoundConstProperty(c, MOLAR_MASS);
				const double mollFrac = GetCompoundFractionMoll(_time, c, _phase);
				numerator += mollFrac * visco * std::sqrt(mollMass);
				denominator += mollFrac * std::sqrt(mollMass);
			}
			if (denominator != 0.0)
				return numerator / denominator;
			break;
		}
		case EPhase::SOLID:
			for (const auto& c : m_compounds)
				res += GetCompoundFraction(_time, c, _phase) * m_materialsDB->GetTPPropertyValue(c, _property, T, P);
			return res;
		case EPhase::UNDEFINED: return {};
		}
		break;
	case THERMAL_CONDUCTIVITY:
		switch (_phase)
		{
		case EPhase::LIQUID:
			for (const auto& c : m_compounds)
				res += GetCompoundFractionMoll(_time, c, _phase) / std::pow(m_materialsDB->GetTPPropertyValue(c, _property, T, P), 2.0);
			if (res != 0.0)
				return 1.0 / std::sqrt(res);
			break;
		case EPhase::VAPOR:
			for (const auto& c1 : m_compounds)
			{
				const double conduct1 = m_materialsDB->GetTPPropertyValue(c1, _property, T, P);
				const double mollMass1 = GetCompoundConstProperty(c1, MOLAR_MASS);
				const double numerator = GetCompoundFractionMoll(_time, c1, _phase) * conduct1;
				double denominator = 0.0;
				for (const auto& c2 : m_compounds)
				{
					const double conduct2 = m_materialsDB->GetTPPropertyValue(c2, _property, T, P);
					const double mollMass2 = GetCompoundConstProperty(c2, MOLAR_MASS);
					const double f = std::pow((1 + std::sqrt(conduct1 / conduct2) * std::pow(mollMass2 / mollMass1, 1.0 / 4.0)), 2) / std::sqrt(8 * (1 + mollMass1 / mollMass2));
					denominator += GetCompoundFractionMoll(_time, c2, _phase) * f;
				}
				if (denominator != 0.0)
					res += numerator / denominator;
			}
			return res;
		case EPhase::SOLID:
			for (const auto& c : m_compounds)
				res += GetCompoundFraction(_time, c, _phase) * m_materialsDB->GetTPPropertyValue(c, _property, T, P);
			return res;
		case EPhase::UNDEFINED: return {};
		}
		break;
	case DENSITY:
		if (_phase == EPhase::SOLID && m_grid->IsDistrTypePresent(DISTR_PART_POROSITY))
		{
			CMatrix2D distr = m_phases.at(_phase)->MDDistr()->GetDistribution(_time, DISTR_COMPOUNDS, DISTR_PART_POROSITY);
			const size_t nCompounds = m_compounds.size();
			const size_t nPorosities = m_grid->GetClassesByDistr(DISTR_PART_POROSITY);
			const std::vector<double> vPorosities = m_grid->GetClassMeansByDistr(DISTR_PART_POROSITY);
			for (size_t iCompound = 0; iCompound < nCompounds; ++iCompound)
			{
				const double density = GetCompoundTPProperty(_time, m_compounds[iCompound], DENSITY);
				for (size_t iPoros = 0; iPoros < nPorosities; ++iPoros)
					res += density * (1 - vPorosities[iPoros]) * distr[iCompound][iPoros];
			}
			return res;
		}
		else
		{
			for (const auto& c : m_compounds)
			{
				const double componentDensity = m_materialsDB->GetTPPropertyValue(c, _property, T, P);
				if (componentDensity != 0.0)
					res += GetCompoundFraction(_time, c, _phase) / componentDensity;
			}
			if (res != 0.0)
				return 1.0 / res;
		}
		break;
	case ENTHALPY:
	case PERMITTIVITY:
	case TP_PROP_USER_DEFINED_01:
	case TP_PROP_USER_DEFINED_02:
	case TP_PROP_USER_DEFINED_03:
	case TP_PROP_USER_DEFINED_04:
	case TP_PROP_USER_DEFINED_05:
	case TP_PROP_USER_DEFINED_06:
	case TP_PROP_USER_DEFINED_07:
	case TP_PROP_USER_DEFINED_08:
	case TP_PROP_USER_DEFINED_09:
	case TP_PROP_USER_DEFINED_10:
	case TP_PROP_USER_DEFINED_11:
	case TP_PROP_USER_DEFINED_12:
	case TP_PROP_USER_DEFINED_13:
	case TP_PROP_USER_DEFINED_14:
	case TP_PROP_USER_DEFINED_15:
	case TP_PROP_USER_DEFINED_16:
	case TP_PROP_USER_DEFINED_17:
	case TP_PROP_USER_DEFINED_18:
	case TP_PROP_USER_DEFINED_19:
	case TP_PROP_USER_DEFINED_20:
		for (const auto& c : m_compounds)
			res += GetCompoundFraction(_time, c, _phase) * m_materialsDB->GetTPPropertyValue(c, _property, T, P);
		return res;
	case TP_PROP_NO_PROERTY: break;
	}

	return {};
}

void CBaseStream::SetPhaseFraction(double _time, EPhase _phase, double _value)
{
	if (!HasPhase(_phase)) return;

	AddTimePoint(_time);
	m_phases[_phase]->SetFraction(_time, _value);
}

void CBaseStream::SetPhaseMass(double _time, EPhase _phase, double _value)
{
	if (!HasPhase(_phase)) return;

	// get current masses
	double totalMass = GetMass(_time);
	std::map<EPhase, double> phaseMasses;
	for (const auto& [state, phase] : m_phases)
		phaseMasses[state] = GetPhaseMass(_time, state);

	// calculate adjustment for the total mass
	totalMass += _value - phaseMasses[_phase];

	// set new phase fractions according to the changes phase masses
	phaseMasses[_phase] = _value;
	for (const auto& [state, phase] : m_phases)
		if (totalMass != 0.0)
			m_phases[state]->SetFraction(_time, phaseMasses[state] / totalMass);
		else
			m_phases[state]->SetFraction(_time, 0.0);

	// set new total mass
	SetMass(_time, totalMass);
}

double CBaseStream::GetMixtureProperty(double _time, EOverall _property) const
{
	return GetOverallProperty(_time, _property);
}

double CBaseStream::GetMixtureProperty(double _time, ECompoundConstProperties _property) const
{
	double res = 0.0;
	for (const auto& [state, phase] : m_phases)
		res += GetPhaseProperty(_time, state, _property) * phase->GetFraction(_time);
	return res;
}

double CBaseStream::GetMixtureProperty(double _time, ECompoundTPProperties _property) const
{
	double res = 0.0;
	for (const auto& [state, phase] : m_phases)
		res += GetPhaseProperty(_time, state, _property) * phase->GetFraction(_time);
	return res;
}

void CBaseStream::SetMixtureProperty(double _time, EOverall _property, double _value)
{
	SetOverallProperty(_time, _property, _value);
}

double CBaseStream::GetCompoundConstProperty(const std::string& _compoundKey, ECompoundConstProperties _property) const
{
	if (!m_materialsDB) return {};

	return m_materialsDB->GetConstPropertyValue(_compoundKey, _property);
}

double CBaseStream::GetCompoundTPProperty(const std::string& _compoundKey, ECompoundTPProperties _property, double _temperature, double _pressure) const
{
	if (!m_materialsDB) return {};

	return m_materialsDB->GetTPPropertyValue(_compoundKey, _property, _temperature, _pressure);
}

double CBaseStream::GetCompoundTPProperty(double _time, const std::string& _compoundKey, ECompoundTPProperties _property) const
{
	return GetCompoundTPProperty(_compoundKey, _property, GetTemperature(_time), GetPressure(_time));
}

double CBaseStream::GetCompoundInteractionProperty(const std::string& _compoundKey1, const std::string& _compoundKey2, EInteractionProperties _property, double _temperature, double _pressure) const
{
	if (!m_materialsDB) return {};

	return m_materialsDB->GetInteractionPropertyValue(_compoundKey1, _compoundKey2, _property, _temperature, _pressure);
}

double CBaseStream::GetCompoundInteractionProperty(double _time, const std::string& _compoundKey1, const std::string& _compoundKey2, EInteractionProperties _property) const
{
	return GetCompoundInteractionProperty(_compoundKey1, _compoundKey2, _property, GetTemperature(_time), GetPressure(_time));
}

double CBaseStream::GetFraction(double _time, const std::vector<size_t>& _coords) const
{
	if (!HasPhase(EPhase::SOLID)) return {};

	return m_phases.at(EPhase::SOLID)->MDDistr()->GetValue(_time, vector_cast<unsigned>(_coords));
}

void CBaseStream::SetFraction(double _time, const std::vector<size_t>& _coords, double _value)
{
	if (!HasPhase(EPhase::SOLID)) return;

	// TODO: remove when it is not needed by MD matrix
	AddTimePoint(_time);
	m_phases.at(EPhase::SOLID)->MDDistr()->SetValue(_time, vector_cast<unsigned>(_coords), _value);
}

std::vector<double> CBaseStream::GetDistribution(double _time, EDistrTypes _distribution) const
{
	if (!HasPhase(EPhase::SOLID)) return {};

	return m_phases.at(EPhase::SOLID)->MDDistr()->GetDistribution(_time, _distribution);
}

CMatrix2D CBaseStream::GetDistribution(double _time, EDistrTypes _distribution1, EDistrTypes _distribution2) const
{
	if (!HasPhase(EPhase::SOLID)) return {};

	return m_phases.at(EPhase::SOLID)->MDDistr()->GetDistribution(_time, _distribution1, _distribution2);
}

CDenseMDMatrix CBaseStream::GetDistribution(double _time, const std::vector<EDistrTypes>& _distributions) const
{
	if (!HasPhase(EPhase::SOLID)) return {};

	return m_phases.at(EPhase::SOLID)->MDDistr()->GetDistribution(_time, vector_cast<unsigned>(_distributions));
}

std::vector<double> CBaseStream::GetDistribution(double _time, EDistrTypes _distribution, const std::string& _compoundKey) const
{
	if (!HasPhase(EPhase::SOLID)) return {};
	if (!HasCompound(_compoundKey)) return {};
	if (_distribution == DISTR_COMPOUNDS) return {};

	return ::Normalize(m_phases.at(EPhase::SOLID)->MDDistr()->GetVectorValue(_time, DISTR_COMPOUNDS, static_cast<unsigned>(CompoundIndex(_compoundKey)), _distribution));
}

CMatrix2D CBaseStream::GetDistribution(double _time, EDistrTypes _distribution1, EDistrTypes _distribution2, const std::string& _compoundKey) const
{
	if (!HasPhase(EPhase::SOLID)) return {};
	if (!HasCompound(_compoundKey)) return {};
	if (_distribution1 == DISTR_COMPOUNDS || _distribution2 == DISTR_COMPOUNDS) return {};

	// prepare coordinates
	const size_t size1 = m_grid->GetClassesByDistr(_distribution1);
	const size_t size2 = m_grid->GetClassesByDistr(_distribution2);
	const std::vector<EDistrTypes> dims{ DISTR_COMPOUNDS, _distribution1, _distribution2 };
	std::vector<size_t> coords{ CompoundIndex(_compoundKey), 0 };
	CMatrix2D res{ size1, size2 };

	// gather data
	for (size_t i = 0; i < size1; ++i)
	{
		res.SetRow(i, m_phases.at(EPhase::SOLID)->MDDistr()->GetVectorValue(_time, vector_cast<unsigned>(dims), vector_cast<unsigned>(coords)));
		coords.back()++;
	}
	res.Normalize();

	return res;
}

CDenseMDMatrix CBaseStream::GetDistribution(double _time, const std::vector<EDistrTypes>& _distributions, const std::string& _compoundKey) const
{
	if (!HasPhase(EPhase::SOLID)) return {};
	if (!HasCompound(_compoundKey)) return {};
	if (VectorContains(_distributions, DISTR_COMPOUNDS)) return {};

	// prepare common parameters
	std::vector<size_t> sizes(_distributions.size());
	for (size_t i = 0; i < _distributions.size(); ++i)
		sizes[i] = m_grid->GetClassesByDistr(_distributions[i]);
	CDenseMDMatrix res(vector_cast<unsigned>(_distributions), vector_cast<unsigned>(sizes));

	// prepare parameters for reading
	std::vector<EDistrTypes> getDims = _distributions;
	getDims.insert(getDims.begin(), DISTR_COMPOUNDS);
	std::vector<size_t> getCoords(_distributions.size());
	getCoords.front() = CompoundIndex(_compoundKey);
	std::vector<size_t> getSizes = sizes;
	getSizes.insert(getSizes.begin(), m_grid->GetClassesByDistr(DISTR_COMPOUNDS));
	getSizes.pop_back();

	// prepare parameters for writing
	const std::vector<EDistrTypes>& setDims = _distributions;
	std::vector<size_t> setCoords(_distributions.size() - 1);
	std::vector<size_t> setSizes = sizes;
	setSizes.pop_back();

	// gather data
	bool notFinished;
	do
	{
		std::vector<double> vec = m_phases.at(EPhase::SOLID)->MDDistr()->GetVectorValue(_time, vector_cast<unsigned>(getDims), vector_cast<unsigned>(getCoords));
		res.SetVectorValue(vector_cast<unsigned>(setDims), vector_cast<unsigned>(setCoords), vec);
		IncrementCoords(getCoords, getSizes);
		notFinished = IncrementCoords(setCoords, setSizes);
	} while (notFinished);
	res.Normalize();

	return res;
}

void CBaseStream::SetDistribution(double _time, EDistrTypes _distribution, const std::vector<double>& _value)
{
	if (!HasPhase(EPhase::SOLID)) return;

	AddTimePoint(_time);
	m_phases[EPhase::SOLID]->MDDistr()->SetDistribution(_time, _distribution, _value);
}

void CBaseStream::SetDistribution(double _time, EDistrTypes _distribution1, EDistrTypes _distribution2, const CMatrix2D& _value)
{
	if (!HasPhase(EPhase::SOLID)) return;

	// TODO: remove when it is not needed by MD matrix
	AddTimePoint(_time);
	m_phases[EPhase::SOLID]->MDDistr()->SetDistribution(_time, _distribution1, _distribution2, _value);
}

void CBaseStream::SetDistribution(double _time, const CDenseMDMatrix& _value)
{
	if (!HasPhase(EPhase::SOLID)) return;

	// TODO: remove when it is not needed by MD matrix
	AddTimePoint(_time);
	m_phases[EPhase::SOLID]->MDDistr()->SetDistribution(_time, _value);
}

void CBaseStream::SetDistribution(double _time, EDistrTypes _distribution, const std::string& _compoundKey, const std::vector<double>& _value)
{
	if (!HasPhase(EPhase::SOLID)) return;
	if (!HasCompound(_compoundKey)) return;
	if (_distribution == DISTR_COMPOUNDS) return;

	// TODO: remove when it is not needed by MD matrix
	AddTimePoint(_time);
	m_phases[EPhase::SOLID]->MDDistr()->SetVectorValue(_time, DISTR_COMPOUNDS, static_cast<unsigned>(CompoundIndex(_compoundKey)), _distribution, _value, true);
	m_phases[EPhase::SOLID]->MDDistr()->NormalizeMatrix(_time);
}

void CBaseStream::SetDistribution(double _time, EDistrTypes _distribution1, EDistrTypes _distribution2, const std::string& _compoundKey, const CMatrix2D& _value)
{
	if (!HasPhase(EPhase::SOLID)) return;
	if (!HasCompound(_compoundKey)) return;
	if (_distribution1 == DISTR_COMPOUNDS || _distribution2 == DISTR_COMPOUNDS) return;

	// TODO: remove when it is not needed by MD matrix
	AddTimePoint(_time);

	// prepare common parameters
	std::vector<EDistrTypes> inDims{ _distribution1, _distribution2 };
	std::vector<size_t> inSizes(inDims.size());
	for (size_t i = 0; i < inDims.size(); ++i)
		inSizes[i] = m_grid->GetClassesByDistr(inDims[i]);

	// prepare parameters for reading from input 2D-distribution
	const size_t size1 = m_grid->GetClassesByDistr(_distribution1);

	// prepare parameters for writing to output MD-distribution
	std::vector<EDistrTypes> setDims = inDims;
	setDims.insert(setDims.begin(), DISTR_COMPOUNDS);
	std::vector<size_t> setCoords(inDims.size());
	setCoords.front() = CompoundIndex(_compoundKey);
	std::vector<size_t> setSizes = inSizes;
	setSizes.insert(setSizes.begin(), m_grid->GetClassesByDistr(DISTR_COMPOUNDS));

	// get old distribution with compounds
	std::vector<EDistrTypes> fullDims = inDims;
	fullDims.insert(fullDims.begin(), DISTR_COMPOUNDS);
	CDenseMDMatrix matrixMD = m_phases[EPhase::SOLID]->MDDistr()->GetDistribution(_time, vector_cast<unsigned>(fullDims));

	// set new values from 2D-distribution to MD-distribution
	for (size_t i = 0; i < size1; ++i)
	{
		matrixMD.SetVectorValue(vector_cast<unsigned>(setDims), vector_cast<unsigned>(setCoords), _value.GetRow(i));
		IncrementCoords(setCoords, setSizes);
	}

	// set output MD-distribution into main distribution
	matrixMD.Normalize();
	m_phases[EPhase::SOLID]->MDDistr()->SetDistribution(_time, matrixMD);
}

void CBaseStream::SetDistribution(double _time, const std::string& _compoundKey, const CDenseMDMatrix& _value)
{
	if (!HasPhase(EPhase::SOLID)) return;
	if (!HasCompound(_compoundKey)) return;
	// TODO: remove cast
	if (VectorContains(_value.GetDimensions(), E2I(DISTR_COMPOUNDS))) return;

	// TODO: remove when it is not needed by MD matrix
	AddTimePoint(_time);

	// prepare common parameters
	// TODO: remove cast
	std::vector<EDistrTypes> inDims = vector_cast<EDistrTypes>(_value.GetDimensions());
	std::vector<size_t> inSizes(inDims.size());
	for (size_t i = 0; i < inDims.size(); ++i)
		inSizes[i] = m_grid->GetClassesByDistr(inDims[i]);

	// prepare parameters for reading from input MD-distribution
	const std::vector<EDistrTypes> getDims = inDims;
	std::vector<size_t> getCoords(inDims.size() - 1);
	const std::vector<size_t> getSizes = inSizes;

	// prepare parameters for writing to output MD-distribution
	std::vector<EDistrTypes> setDims = inDims;
	setDims.insert(setDims.begin(), DISTR_COMPOUNDS);
	std::vector<size_t> setCoords(inDims.size());
	setCoords.front() = CompoundIndex(_compoundKey);
	std::vector<size_t> setSizes = inSizes;
	setSizes.insert(setSizes.begin(), m_grid->GetClassesByDistr(DISTR_COMPOUNDS));

	// get old distribution with compounds
	std::vector<EDistrTypes> fullDims = inDims;
	fullDims.insert(fullDims.begin(), DISTR_COMPOUNDS);
	CDenseMDMatrix matrixMD = m_phases[EPhase::SOLID]->MDDistr()->GetDistribution(_time, vector_cast<unsigned>(fullDims));

	// set new values from input MD-distribution to output MD-distribution
	bool notFinished;
	do
	{
		matrixMD.SetVectorValue(vector_cast<unsigned>(setDims), vector_cast<unsigned>(setCoords), _value.GetVectorValue(vector_cast<unsigned>(getDims), vector_cast<unsigned>(getCoords)));
		IncrementCoords(setCoords, setSizes);
		notFinished = IncrementCoords(getCoords, getSizes);
	} while (notFinished);

	// set output MD-distribution into main distribution
	matrixMD.Normalize();
	m_phases[EPhase::SOLID]->MDDistr()->SetDistribution(_time, matrixMD);
}

void CBaseStream::ApplyTM(double _time, const CTransformMatrix& _matrix)
{
	if (!HasPhase(EPhase::SOLID)) return;

	m_phases[EPhase::SOLID]->MDDistr()->Transform(_time, _matrix);
	m_phases[EPhase::SOLID]->MDDistr()->NormalizeMatrix(_time);
}

void CBaseStream::ApplyTM(double _time, const std::string& _compoundKey, const CTransformMatrix& _matrix)
{
	if (!HasPhase(EPhase::SOLID)) return;
	if (!HasCompound(_compoundKey)) return;
	if (_matrix.GetDimensionsNumber() == 0) return;
	// TODO: remove cast
	if (VectorContains(_matrix.GetDimensions(), E2I(DISTR_COMPOUNDS))) return;

	// prepare all parameters
	const size_t iCompound = CompoundIndex(_compoundKey);
	// TODO: remove cast
	std::vector<EDistrTypes> inDims = vector_cast<EDistrTypes>(_matrix.GetDimensions());
	std::vector<EDistrTypes> newDims = inDims;
	newDims.insert(newDims.begin(), DISTR_COMPOUNDS);
	// TODO: remove cast
	std::vector<size_t> oldSizes = vector_cast<size_t>(_matrix.GetClasses());
	std::vector<size_t> oldSizesFull = oldSizes;
	oldSizesFull.insert(oldSizesFull.end(), oldSizes.begin(), oldSizes.end() - 1);
	std::vector<size_t> newSizes = oldSizes;
	newSizes.insert(newSizes.begin(), m_grid->GetClassesByDistr(DISTR_COMPOUNDS));
	std::vector<size_t> oldCoordsFull(oldSizes.size() * 2 - 1);
	std::vector<size_t> newCoords(newSizes.size());
	newCoords.front() = iCompound;

	// create new transformation matrix
	CTransformMatrix newTM(vector_cast<unsigned>(newDims), vector_cast<unsigned>(newSizes));

	// copy values from old to new TM
	bool notFinished;
	size_t oldSrcSize = newDims.size() - 1;
	size_t oldDstSize = newDims.size() - 2;
	size_t newSrcSize = newDims.size();
	size_t newDstSize = newDims.size() - 1;
	std::vector<size_t> oldSrcCoords(oldSrcSize);
	std::vector<size_t> oldDstCoords(oldDstSize);
	std::vector<size_t> newSrcCoords(newSrcSize);
	std::vector<size_t> newDstCoords(newDstSize);
	newSrcCoords.front() = iCompound;
	newDstCoords.front() = iCompound;
	do
	{
		std::copy_n(oldCoordsFull.begin(), oldSrcSize, oldSrcCoords.begin());
		std::copy(oldCoordsFull.begin() + static_cast<std::vector<size_t>::difference_type>(oldSrcSize), oldCoordsFull.end(), oldDstCoords.begin());
		std::copy_n(oldCoordsFull.begin(), oldSrcSize, newSrcCoords.begin() + 1);
		std::copy(oldCoordsFull.begin() + static_cast<std::vector<size_t>::difference_type>(oldSrcSize), oldCoordsFull.end(), newDstCoords.begin() + 1);
		std::vector<double> vec = _matrix.GetVectorValue(vector_cast<unsigned>(oldSrcCoords), vector_cast<unsigned>(oldDstCoords));
		newTM.SetVectorValue(vector_cast<unsigned>(newSrcCoords), vector_cast<unsigned>(newDstCoords), vec);
		notFinished = IncrementCoords(oldCoordsFull, oldSizesFull);
	} while (notFinished);

	// set values for other compounds
	std::fill(newCoords.begin(), newCoords.end(), 0);
	notFinished = true;
	do
	{
		if (newCoords.front() != iCompound)
			newTM.SetValue(vector_cast<unsigned>(newCoords), vector_cast<unsigned>(newCoords), 1);
		notFinished = IncrementCoords(newCoords, newSizes);
	} while (notFinished);

	// transform
	m_phases[EPhase::SOLID]->MDDistr()->Transform(_time, newTM);
	m_phases[EPhase::SOLID]->MDDistr()->NormalizeMatrix(_time);
}

void CBaseStream::Normalize(double _time)
{
	if (!HasPhase(EPhase::SOLID)) return;

	return m_phases[EPhase::SOLID]->MDDistr()->NormalizeMatrix(_time);
}

void CBaseStream::Normalize(double _timeBeg, double _timeEnd)
{
	if (!HasPhase(EPhase::SOLID)) return;

	return m_phases[EPhase::SOLID]->MDDistr()->NormalizeMatrix(_timeBeg, _timeEnd);
}

void CBaseStream::Normalize()
{
	if (!HasPhase(EPhase::SOLID)) return;

	return m_phases[EPhase::SOLID]->MDDistr()->NormalizeMatrix();
}

std::vector<double> CBaseStream::GetPSD(double _time, EPSDTypes _type, EPSDGridType _grid) const
{
	return GetPSD(_time, _type, std::vector<std::string>{}, _grid);
}

std::vector<double> CBaseStream::GetPSD(double _time, EPSDTypes _type, const std::string& _compoundKey, EPSDGridType _grid) const
{
	return GetPSD(_time, _type, std::vector<std::string>(1, _compoundKey), _grid);
}

std::vector<double> CBaseStream::GetPSD(double _time, EPSDTypes _type, const std::vector<std::string>& _compoundKeys, EPSDGridType _grid) const
{
	if (!HasPhase(EPhase::SOLID)) return {};
	if (!m_grid->IsDistrTypePresent(DISTR_SIZE)) return {};
	if (!HasCompounds(_compoundKeys)) return {};

	switch (_type)
	{
	case PSD_MassFrac:	return GetPSDMassFraction(_time, _compoundKeys);
	case PSD_Number:	return GetPSDNumber(_time, _compoundKeys, _grid);
	case PSD_q3:		return ConvertMassFractionsToq3(m_grid->GetPSDGrid(_grid), GetPSDMassFraction(_time, _compoundKeys));
	case PSD_Q3:		return ConvertMassFractionsToQ3(GetPSDMassFraction(_time, _compoundKeys));
	case PSD_q0:		return ConvertNumbersToq0(m_grid->GetPSDGrid(_grid), GetPSDNumber(_time, _compoundKeys, _grid));
	case PSD_Q0:		return ConvertNumbersToQ0(m_grid->GetPSDGrid(_grid), GetPSDNumber(_time, _compoundKeys, _grid));
	case PSD_q2:		return ConvertNumbersToq2(m_grid->GetPSDGrid(_grid), GetPSDNumber(_time, _compoundKeys, _grid));
	case PSD_Q2:		return ConvertNumbersToQ2(m_grid->GetPSDGrid(_grid), GetPSDNumber(_time, _compoundKeys, _grid));
	}

	return {};
}

void CBaseStream::SetPSD(double _time, EPSDTypes _type, const std::vector<double>& _value, EPSDGridType _grid)
{
	SetPSD(_time, _type, "", _value, _grid);
}

void CBaseStream::SetPSD(double _time, EPSDTypes _type, const std::string& _compoundKey, const std::vector<double>& _value, EPSDGridType _grid)
{
	if (!HasPhase(EPhase::SOLID)) return;
	if (!m_grid->IsDistrTypePresent(DISTR_SIZE)) return;

	AddTimePoint(_time);

	// construct distribution
	std::vector<double> distr;
	switch (_type)
	{
	case PSD_MassFrac:	distr = _value;																break;
	case PSD_Number:	distr = ConvertNumbersToMassFractions(m_grid->GetPSDGrid(_grid), _value);	break;
	case PSD_q3:		distr = Convertq3ToMassFractions(m_grid->GetPSDGrid(_grid), _value);		break;
	case PSD_Q3:		distr = ConvertQ3ToMassFractions(_value);									break;
	case PSD_q0:		distr = Convertq0ToMassFractions(m_grid->GetPSDGrid(_grid), _value);		break;
	case PSD_Q0:		distr = ConvertQ0ToMassFractions(m_grid->GetPSDGrid(_grid), _value);		break;
	case PSD_q2:		distr = Convertq2ToMassFractions(m_grid->GetPSDGrid(_grid), _value);	    break;
	case PSD_Q2:		distr = ConvertQ2ToMassFractions(m_grid->GetPSDGrid(_grid), _value);		break;
	}
	::Normalize(distr);

	// set distribution
	if (_compoundKey.empty())			// for the total mixture
		m_phases[EPhase::SOLID]->MDDistr()->SetDistribution(_time, DISTR_SIZE, distr);
	else if (HasCompound(_compoundKey))	// for specific compound
		m_phases[EPhase::SOLID]->MDDistr()->SetVectorValue(_time, DISTR_COMPOUNDS, static_cast<unsigned>(CompoundIndex(_compoundKey)), DISTR_SIZE, distr, true);
	m_phases[EPhase::SOLID]->MDDistr()->NormalizeMatrix(_time);
}

void CBaseStream::Copy(double _time, const CBaseStream& _source)
{
	if (!HaveSameStructure(*this, _source)) return;

	// remove redundant time points
	RemoveTimePointsAfter(_time);

	// insert time point
	InsertTimePoint(_time);

	// copy data in overall parameters
	for (auto& [type, param] : m_overall)
		param->CopyFrom(_time, *_source.m_overall.at(type));

	// copy data in phases
	for (auto& [key, param] : m_phases)
		param->CopyFrom(_time, *_source.m_phases.at(key));
}

void CBaseStream::Copy(double _timeBeg, double _timeEnd, const CBaseStream& _source)
{
	if (!HaveSameStructure(*this, _source)) return;

	// remove redundant time points
	RemoveTimePointsAfter(_timeBeg, true);

	// insert time points
	for (double t : _source.GetTimePoints(_timeBeg, _timeEnd))
		InsertTimePoint(t);

	// copy data in overall parameters
	for (auto& [type, param] : m_overall)
		param->CopyFrom(_timeBeg, _timeEnd, *_source.m_overall.at(type));

	// copy data in phases
	for (auto& [key, param] : m_phases)
		param->CopyFrom(_timeBeg, _timeEnd, *_source.m_phases.at(key));
}

void CBaseStream::Copy(double _timeDst, const CBaseStream& _source, double _timeSrc)
{
	if (!HaveSameStructure(*this, _source)) return;

	// remove redundant time points
	RemoveTimePointsAfter(_timeDst);

	// insert time point
	InsertTimePoint(_timeDst);

	// copy data in overall parameters
	for (auto& [type, param] : m_overall)
		param->CopyFrom(_timeDst, *_source.m_overall.at(type), _timeSrc);

	// copy data in phases
	for (auto& [key, param] : m_phases)
		param->CopyFrom(_timeDst, *_source.m_phases.at(key), _timeSrc);
}

void CBaseStream::Add(double _time, const CBaseStream& _source)
{
	Add(_time, _time, _source);
}

void CBaseStream::Add(double _timeBeg, double _timeEnd, const CBaseStream& _source)
{
	if (!HaveSameStructure(*this, _source)) return;

	// gather all time points
	std::vector<double> timePoints = VectorsUnionSorted(_source.GetTimePoints(_timeBeg, _timeEnd), GetTimePoints(_timeBeg, _timeEnd));

	// prepare temporary vector for all data for each time point
	std::vector<mix_type> mix(timePoints.size());

	// calculate mixture for each time point
	for (size_t i = 0; i < timePoints.size(); ++i)
	{
		// get masses
		const double massSrc = _source.GetMass(timePoints[i]);
		const double massDst = GetMass(timePoints[i]);

		// calculate mixture
		mix[i] = CalculateMix(timePoints[i], _source, massSrc, timePoints[i], *this, massDst);
	}

	// set mixture data for each time point
	for (size_t i = 0; i < timePoints.size(); ++i)
		SetMix(*this, timePoints[i], mix[i]);
}

bool CBaseStream::AreEqual(double _time, const CBaseStream& _stream1, const CBaseStream& _stream2, double _absTol, double _relTol)
{
	const auto& Same = [&](double _v1, double _v2)
	{
		return std::fabs(_v1 - _v2) < std::fabs(_v1) * _relTol + _absTol;
	};

	if (!HaveSameStructure(_stream1, _stream2)) return false;

	// overall parameters
	for (const auto& [key, param] : _stream1.m_overall)
		if (!Same(param->GetValue(_time), _stream2.m_overall.at(key)->GetValue(_time)))
			return false;

	// phases
	for (const auto& [key, param] : _stream1.m_phases)
	{
		if (!Same(param->GetFraction(_time), _stream2.m_phases.at(key)->GetFraction(_time)))
			return false;

		const auto distr1 = param->MDDistr()->GetDistribution(_time);
		const auto distr2 = _stream2.m_phases.at(key)->MDDistr()->GetDistribution(_time);
		const double* arr1 = distr1.GetDataPtr();
		const double* arr2 = distr2.GetDataPtr();
		for (size_t i = 0; i < distr1.GetDataLength(); ++i)
			if (std::fabs(arr1[i] - arr2[i]) > std::fabs(arr1[i]) * _relTol + _absTol)
				return false;
	}

	return true;
}

CLookupTables* CBaseStream::GetLookupTables()
{
	return &m_lookupTables;
}

const CMaterialsDatabase* CBaseStream::GetMaterialsDatabase() const
{
	return m_materialsDB;
}

void CBaseStream::SetMaterialsDatabase(const CMaterialsDatabase* _database)
{
	m_materialsDB = _database;
}

void CBaseStream::SetGrid(const CDistributionsGrid* _grid)
{
	m_grid = _grid;
}

void CBaseStream::UpdateMDGrid()
{
	for (auto& [state, phase] : m_phases)
		phase->UpdateMDGrid();
}

void CBaseStream::SetCacheSettings(const SCacheSettings& _settings)
{
	m_cacheSettings = _settings;

	// overall parameters
	for (auto& [type, param] : m_overall)
		param->SetCacheSettings(_settings);

	// phases
	for (auto& [state, phase] : m_phases)
		phase->SetCacheSettings(_settings);
}

void CBaseStream::SetMinimumFraction(double _fraction)
{
	for (auto& [state, phase] : m_phases)
		phase->MDDistr()->SetMinimalFraction(_fraction);
}

void CBaseStream::Extrapolate(double _timeExtra, double _time)
{
	if (_time >= _timeExtra) return;

	// manage time points
	RemoveTimePointsAfter(_time, false);
	InsertTimePoint(_timeExtra);

	// extrapolate overall parameters
	for (auto& [type, param] : m_overall)
		param->Extrapolate(_timeExtra, _time);

	// extrapolate phases
	for (auto& [state, phase] : m_phases)
		phase->Extrapolate(_timeExtra, _time);
}

void CBaseStream::Extrapolate(double _timeExtra, double _time1, double _time2)
{
	if (_time1 >= _time2 || _time2 >= _timeExtra) return;

	// manage time points
	RemoveTimePointsAfter(_time2, false);
	InsertTimePoint(_timeExtra);

	// extrapolate overall parameters
	for (auto& [type, param] : m_overall)
		param->Extrapolate(_timeExtra, _time1, _time2);

	// extrapolate phases
	for (auto& [state, phase] : m_phases)
		phase->Extrapolate(_timeExtra, _time1, _time2);
}

void CBaseStream::Extrapolate(double _timeExtra, double _time1, double _time2, double _time3)
{
	if (_time1 >= _time2 || _time2 >= _time3 || _time3 >= _timeExtra) return;

	// manage time points
	RemoveTimePointsAfter(_time3, false);
	InsertTimePoint(_timeExtra);

	// extrapolate overall parameters
	for (auto& [type, param] : m_overall)
		param->Extrapolate(_timeExtra, _time1, _time2, _time3);

	// extrapolate phases
	for (auto& [state, phase] : m_phases)
		phase->Extrapolate(_timeExtra, _time1, _time2, _time3);
}

void CBaseStream::SaveToFile(CH5Handler& _h5File, const std::string& _path)
{
	if (!_h5File.IsValid()) return;

	// current version of save procedure
	_h5File.WriteAttribute(_path, StrConst::Stream_H5AttrSaveVersion, m_saveVersion);

	// stream name
	_h5File.WriteData(_path, StrConst::Stream_H5StreamName, m_name);

	// stream key
	_h5File.WriteData(_path, StrConst::Stream_H5StreamKey, m_key);

	// time points
	_h5File.WriteData(_path, StrConst::Stream_H5TimePoints, m_timePoints);

	// compounds
	_h5File.WriteData(_path, StrConst::Stream_H5Compounds, m_compounds);

	// overall properties
	_h5File.WriteData(_path, StrConst::Stream_H5OverallKeys, E2I(MapKeys(m_overall)));
	const std::string groupOveralls = _h5File.CreateGroup(_path, StrConst::Stream_H5GroupOveralls);
	size_t iOverall = 0;
	for (auto& [type, param] : m_overall)
	{
		const std::string overallPath = _h5File.CreateGroup(groupOveralls, StrConst::Stream_H5GroupOverallName + std::to_string(iOverall++));
		param->SaveToFile(_h5File, overallPath);
	}

	// phases
	_h5File.WriteData(_path, StrConst::Stream_H5PhaseKeys, E2I(MapKeys(m_phases)));
	const std::string groupPhases = _h5File.CreateGroup(_path, StrConst::Stream_H5GroupPhases);
	size_t iPhase = 0;
	for (auto& [state, phase] : m_phases)
	{
		const std::string phasePath = _h5File.CreateGroup(groupPhases, StrConst::Stream_H5GroupPhaseName + std::to_string(iPhase++));
		phase->SaveToFile(_h5File, phasePath);
	}
}

void CBaseStream::LoadFromFile(CH5Handler& _h5File, const std::string& _path)
{
	if (!_h5File.IsValid()) return;

	// current version of save procedure
	const int version = _h5File.ReadAttribute(_path, StrConst::Stream_H5AttrSaveVersion);

	// compatibility with older versions
	if (version < 2)
	{
		LoadFromFile_v1(_h5File, _path);
		return;
	}

	// clear current state
	m_timePoints.clear();
	m_overall.clear();
	m_phases.clear();
	m_compounds.clear();
	m_lookupTables.Clear();

	// basic data
	_h5File.ReadData(_path, StrConst::Stream_H5StreamName, m_name);
	_h5File.ReadData(_path, StrConst::Stream_H5StreamKey,  m_key);
	_h5File.ReadData(_path, StrConst::Stream_H5TimePoints, m_timePoints);
	_h5File.ReadData(_path, StrConst::Stream_H5Compounds,  m_compounds);

	// overall properties
	std::vector<unsigned> keys;
	_h5File.ReadData(_path, StrConst::Stream_H5OverallKeys, keys);
	std::vector<EOverall> overallKeys = vector_cast<EOverall>(keys);
	size_t iOverall = 0;
	for (auto key : overallKeys)
	{
		std::string overallPath = _path + "/" + StrConst::Stream_H5GroupOveralls + "/" + StrConst::Stream_H5GroupOverallName + std::to_string(iOverall++);
		AddOverallProperty(key)->LoadFromFile(_h5File, overallPath);
	}

	// phases
	_h5File.ReadData(_path, StrConst::Stream_H5PhaseKeys, keys);
	std::vector<EPhase> phaseKeys = vector_cast<EPhase>(keys);
	size_t iPhase = 0;
	for (auto key : phaseKeys)
	{
		std::string phasePath = _path + "/" + StrConst::Stream_H5GroupPhases + "/" + StrConst::Stream_H5GroupPhaseName + std::to_string(iPhase++);
		AddPhase(key)->LoadFromFile(_h5File, phasePath);
	}
}

void CBaseStream::LoadFromFile_v1(CH5Handler& _h5File, const std::string& _path)
{
	const auto& AddOverall = [&](EOverall _type, const std::vector<std::vector<double>>& _data, const std::string& _name, const std::string& _units)
	{
		CTimeDependentValue* overall = AddOverallProperty(_type);
		overall->SetRawData(_data);
		overall->SetName(_name);
		overall->SetUnits(_units);
	};

	const auto& Transpose = [](const std::vector<std::vector<double>>& _vec)
	{
		if (_vec.empty()) return std::vector<std::vector<double>>{};
		std::vector<std::vector<double>> res(_vec.front().size(), std::vector<double>(_vec.size()));
		for (size_t i = 0; i < _vec.size(); ++i)
			for (size_t j = 0; j < _vec[i].size(); ++j)
				res[j][i] = _vec[i][j];
		return res;
	};

	// mass is always present in the stream, but cab be either mass or mass flow, so store it before clear
	const std::string massName = m_overall[EOverall::OVERALL_MASS]->GetName();
	const std::string massUnit = m_overall[EOverall::OVERALL_MASS]->GetUnits();

	// clear current state
	m_timePoints.clear();
	m_overall.clear();
	m_phases.clear();
	m_compounds.clear();
	m_lookupTables.Clear();

	// prepare some values
	const std::string distrPathBase = _path + "/" + StrConst::Stream_H5Group2DDistrs + "/" + StrConst::Stream_H5Group2DDistrName;
	std::vector<double> times;
	std::vector<std::vector<double>> values;

	// basic data
	_h5File.ReadData(_path, StrConst::Stream_H5StreamName, m_name);
	_h5File.ReadData(_path, StrConst::Stream_H5StreamKey,  m_key);
	_h5File.ReadData(_path, StrConst::Stream_H5TimePoints, m_timePoints);
	_h5File.ReadData(_path, StrConst::Stream_H5Compounds,  m_compounds);

	// overall properties
	_h5File.ReadData(distrPathBase + std::to_string(1), StrConst::Distr2D_H5TimePoints, times);
	_h5File.ReadData(distrPathBase + std::to_string(1), StrConst::Distr2D_H5Data, values);
	if (values.empty())
	{
		AddOverall(EOverall::OVERALL_MASS,        { {}, {} }, massName,      massUnit);
		AddOverall(EOverall::OVERALL_TEMPERATURE, { {}, {} }, "Temperature", "K");
		AddOverall(EOverall::OVERALL_PRESSURE,    { {}, {} }, "Pressure",    "Pa");
	}
	else
	{
		if (values.size() == 1 && values.size() != times.size())
			values.resize(times.size(), values.front());
		if (values.front().size() != 3) return;
		const auto data = Transpose(values);
		AddOverall(EOverall::OVERALL_MASS,        { times, data[0] }, massName,      massUnit);
		AddOverall(EOverall::OVERALL_TEMPERATURE, { times, data[1] }, "Temperature", "K");
		AddOverall(EOverall::OVERALL_PRESSURE,    { times, data[2] }, "Pressure",    "Pa");
	}

	// phases
	const int nPhases = _h5File.ReadAttribute(_path, StrConst::Stream_H5AttrPhasesNum);
	_h5File.ReadData(distrPathBase + std::to_string(0), StrConst::Distr2D_H5TimePoints, times);
	_h5File.ReadData(distrPathBase + std::to_string(0), StrConst::Distr2D_H5Data, values);
	if (values.size() == 1 && values.size() != times.size())
		values.resize(times.size(), values.front());
	const auto data = Transpose(values);
	if (nPhases < 0) return;
	for (size_t iPhase = 0; iPhase < static_cast<size_t>(nPhases); ++iPhase)
	{
		std::string phasePath = _path + "/" + StrConst::Stream_H5GroupPhases + "/" + StrConst::Stream_H5GroupPhaseName + std::to_string(iPhase);
		std::string name;
		unsigned soa;
		_h5File.ReadData(phasePath, StrConst::Stream_H5PhaseName, name);
		_h5File.ReadData(phasePath, StrConst::Stream_H5PhaseSOA,  soa);
		CPhase* phase;
		switch (soa)
		{
		case 0:	 phase = AddPhase(EPhase::SOLID);     break;
		case 1:  phase = AddPhase(EPhase::LIQUID);    break;
		case 2:  phase = AddPhase(EPhase::VAPOR);     break;
		default: phase = AddPhase(EPhase::UNDEFINED); break;
		}
		phase->SetName(name);
		phase->MDDistr()->LoadFromFile(_h5File, phasePath);
		if (!data.empty())
			phase->Fractions()->SetRawData({times, data[iPhase]});
	}
}

CBaseStream::mix_type CBaseStream::CalculateMix(double _time1, const CBaseStream& _stream1, double _mass1, double _time2, const CBaseStream& _stream2, double _mass2)
{
	// result data
	std::map<EOverall, double> mixOverall;
	std::map<EPhase, double> mixPhaseFrac;
	std::map<EPhase, CDenseMDMatrix> mixDistr;

	// calculate overall properties
	for (const auto& [type, param] : _stream1.m_overall)
	{
		switch (type)
		{
		case EOverall::OVERALL_MASS:
			mixOverall[type] = _mass1 + _mass2;
			break;
		case EOverall::OVERALL_TEMPERATURE:
			mixOverall[type] = CalculateMixTemperature(_time1, _stream1, _mass1, _time2, _stream2, _mass2);
			break;
		case EOverall::OVERALL_PRESSURE:
			mixOverall[type] = CalculateMixPressure(_time1, _stream1, _time2, _stream2);
			break;
		case EOverall::OVERALL_USER_DEFINED_01:
		case EOverall::OVERALL_USER_DEFINED_02:
		case EOverall::OVERALL_USER_DEFINED_03:
		case EOverall::OVERALL_USER_DEFINED_04:
		case EOverall::OVERALL_USER_DEFINED_05:
		case EOverall::OVERALL_USER_DEFINED_06:
		case EOverall::OVERALL_USER_DEFINED_07:
		case EOverall::OVERALL_USER_DEFINED_08:
		case EOverall::OVERALL_USER_DEFINED_09:
		case EOverall::OVERALL_USER_DEFINED_10:
			mixOverall[type] = CalculateMixOverall(_time1, _stream1, _mass1, _time2, _stream2, _mass2, type);
			break;
		}
	}

	// calculate phases
	for (const auto& [key, param] : _stream1.m_phases)
	{
		mixPhaseFrac[key] = CalculateMixPhaseFractions(_time1, _stream1, _mass1, _time2, _stream2, _mass2, key);
		mixDistr[key] = CalculateMixDistribution(_time1, _stream1, _mass1, _time2, _stream2, _mass2, key);
	}

	return { mixOverall, mixPhaseFrac, mixDistr };
}

void CBaseStream::SetMix(CBaseStream& _stream, double _time, const mix_type& _data)
{
	// add time point
	_stream.AddTimePoint(_time);

	// overall properties
	for (auto& [type, param] : _stream.m_overall)
		param->SetValue(_time, std::get<0>(_data).at(type));

	// phases
	for (auto& [key, param] : _stream.m_phases)
	{
		param->SetFraction(_time, std::get<1>(_data).at(key));
		param->MDDistr()->SetDistribution(_time, std::get<2>(_data).at(key));
	}
}

double CBaseStream::CalculateMixPressure(double _time1, const CBaseStream& _stream1, double _time2, const CBaseStream& _stream2)
{
	const double pressure1 = _stream1.GetPressure(_time1);
	const double pressure2 = _stream2.GetPressure(_time2);
	if (pressure1 == 0.0 || pressure2 == 0.0)
		return std::max(pressure2, pressure1);	// return nonzero pressure, if any
	return std::min(pressure2, pressure1);		// return minimum pressure
}

double CBaseStream::CalculateMixTemperature(double _time1, const CBaseStream& _stream1, double _mass1, double _time2, const CBaseStream& _stream2, double _mass2)
{
	// get enthalpies
	const double enthalpy1 = _stream1.m_lookupTables.CalcFromTemperature(_time1, ECompoundTPProperties::ENTHALPY);
	const double enthalpy2 = _stream2.m_lookupTables.CalcFromTemperature(_time2, ECompoundTPProperties::ENTHALPY);
	// calculate total mass
	const double massMix = _mass1 + _mass2;
	// if no material at all, return some arbitrary temperature
	if (massMix == 0.0)
		return STANDARD_CONDITION_T;
	// calculate (specific) total enthalpy
	const double enthalpyMix = (enthalpy1 * _mass1 + enthalpy2 * _mass2) / massMix;
	// combine both enthalpy tables for mixture enthalpy table
	CLookupTable* lookup1 = _stream1.m_lookupTables.GetLookupTable(ECompoundTPProperties::ENTHALPY, EDependencyTypes::DEPENDENCE_TEMP);
	CLookupTable* lookup2 = _stream2.m_lookupTables.GetLookupTable(ECompoundTPProperties::ENTHALPY, EDependencyTypes::DEPENDENCE_TEMP);
	lookup1->MultiplyTable(_mass1 / massMix);
	lookup1->Add(*lookup2, _mass2 / massMix);
	// read out new temperature
	return lookup1->GetParam(enthalpyMix);
}

double CBaseStream::CalculateMixOverall(double _time1, const CBaseStream& _stream1, double _mass1, double _time2, const CBaseStream& _stream2, double _mass2, EOverall _property)
{
	if (_mass1 + _mass2 == 0.0) return {};

	const double value1 = _stream1.m_overall.at(_property)->GetValue(_time1);
	const double value2 = _stream2.m_overall.at(_property)->GetValue(_time2);
	return (_mass1 * value1 + _mass2 * value2) / (_mass1 + _mass2);
}

double CBaseStream::CalculateMixPhaseFractions(double _time1, const CBaseStream& _stream1, double _mass1, double _time2, const CBaseStream& _stream2, double _mass2, EPhase _phase)
{
	if (_mass1 + _mass2 == 0.0) return {};

	const double fraction1 = _stream1.m_phases.at(_phase)->GetFraction(_time1);
	const double fraction2 = _stream2.m_phases.at(_phase)->GetFraction(_time2);
	return (_mass1 * fraction1 + _mass2 * fraction2) / (_mass1 + _mass2);
}

CDenseMDMatrix CBaseStream::CalculateMixDistribution(double _time1, const CBaseStream& _stream1, double _mass1, double _time2, const CBaseStream& _stream2, double _mass2, EPhase _phase)
{
	const auto& phase1 = _stream1.m_phases.at(_phase);
	const auto& phase2 = _stream2.m_phases.at(_phase);
	const double phaseFrac1 = phase1->GetFraction(_time1);
	const double phaseFrac2 = phase2->GetFraction(_time2);
	CDenseMDMatrix distr1 = phase1->MDDistr()->GetDistribution(_time1);
	CDenseMDMatrix distr2 = phase2->MDDistr()->GetDistribution(_time2);
	CDenseMDMatrix mix = distr1 * _mass1 * phaseFrac1 + distr2 * _mass2 * phaseFrac2;

	// TODO: more elegant/effective solution
	// hack to save compounds distributions, if all compounds are set to 0
	//std::vector<double> compFracs = mix.GetVectorValue(DISTR_COMPOUNDS);
	//if (std::all_of(compFracs.begin(), compFracs.end(), [](double d) { return d == 0.0; })) // all are equal to 0
	//	mix = distr1 * _mass1 + distr2 * _mass2; // TODO: all secondary dimensions will be also recalculated

	mix.Normalize();
	return mix;
}

void CBaseStream::InsertTimePoint(double _time)
{
	const auto pos = std::lower_bound(m_timePoints.begin(), m_timePoints.end(), _time);
	if (pos == m_timePoints.end())					// all existing times are smaller
		m_timePoints.emplace_back(_time);
	else if (std::fabs(*pos - _time) <= m_epsilon)	// this time already exists
		*pos = _time;
	else											// insert to the right position
		m_timePoints.insert(pos, _time);
}

bool CBaseStream::HasTime(double _time) const
{
	if (m_timePoints.empty()) return false;
	const auto pos = std::lower_bound(m_timePoints.begin(), m_timePoints.end(), _time);
	if (pos == m_timePoints.end()) return false;
	return std::fabs(*pos - _time) <= m_epsilon;
}

bool CBaseStream::HasOverallProperty(EOverall _property) const
{
	return MapContainsKey(m_overall, _property);
}

bool CBaseStream::HasCompound(const std::string& _compoundKey) const
{
	return VectorContains(m_compounds, _compoundKey);
}

bool CBaseStream::HasCompounds(const std::vector<std::string>& _compoundKeys) const
{
	for (const auto& c : _compoundKeys)
		if (!HasCompound(c))
			return false;
	return true;
}

size_t CBaseStream::CompoundIndex(const std::string& _compoundKey) const
{
	for (size_t i = 0; i < m_compounds.size(); ++i)
		if (m_compounds[i] == _compoundKey)
			return i;
	return static_cast<size_t>(-1);
}

bool CBaseStream::HasPhase(EPhase _phase) const
{
	return MapContainsKey(m_phases, _phase);
}

std::vector<double> CBaseStream::GetPSDMassFraction(double _time, const std::vector<std::string>& _compoundKeys) const
{
	// for all available compounds
	if (_compoundKeys.empty() || _compoundKeys.size() == m_compounds.size())
		return m_phases.at(EPhase::SOLID)->MDDistr()->GetDistribution(_time, DISTR_SIZE);

	// only for selected compounds
	std::vector<double> distr(m_grid->GetClassesByDistr(DISTR_SIZE), 0.0);
	for (const auto& key : _compoundKeys)
	{
		std::vector<double> vec = m_phases.at(EPhase::SOLID)->MDDistr()->GetVectorValue(_time, DISTR_COMPOUNDS, static_cast<unsigned>(CompoundIndex(key)), DISTR_SIZE);
		AddVectors(distr, vec, distr);
	}
	::Normalize(distr);
	return distr;
}

std::vector<double> CBaseStream::GetPSDNumber(double _time, const std::vector<std::string>& _compoundKeys, EPSDGridType _grid) const
{
	std::vector<std::string> activeCompounds = _compoundKeys.empty() || _compoundKeys.size() == m_compounds.size() ? m_compounds : _compoundKeys;
	const bool hasPorosity = m_grid->IsDistrTypePresent(DISTR_PART_POROSITY);
	std::vector<double> volumes = _grid == EPSDGridType::VOLUME ? m_grid->GetPSDMeans(EPSDGridType::VOLUME) : DiameterToVolume(m_grid->GetPSDMeans(EPSDGridType::DIAMETER));
	const double totalMass = GetPhaseMass(_time, EPhase::SOLID);
	const size_t nSizeClasses = m_grid->GetClassesByDistr(DISTR_SIZE);

	// single compound with no porosity, only one compound defined in the stream
	if (!hasPorosity && activeCompounds.size() == 1 && m_compounds.size() == 1)
	{
		std::vector<double> res = m_phases.at(EPhase::SOLID)->MDDistr()->GetDistribution(_time, DISTR_SIZE);
		const double density = GetPhaseProperty(_time, EPhase::SOLID, DENSITY);
		if (density == 0.0) return std::vector<double>(nSizeClasses, 0.0);
		for (size_t i = 0; i < res.size(); ++i)
			if (volumes[i] != 0.0)
				res[i] *= totalMass / density / volumes[i];
		return res;
	}

	// single compound with no porosity, several compounds defined in the stream
	if (!hasPorosity && activeCompounds.size() == 1)
	{
		std::vector<double> res = ::Normalize(m_phases.at(EPhase::SOLID)->MDDistr()->GetVectorValue(_time, DISTR_COMPOUNDS, static_cast<unsigned>(CompoundIndex(activeCompounds.front())), DISTR_SIZE));
		const double density = GetPhaseProperty(_time, EPhase::SOLID, DENSITY);
		if (density == 0.0) return std::vector<double>(nSizeClasses, 0.0);
		for (size_t i = 0; i < res.size(); ++i)
			if (volumes[i] != 0.0)
				res[i] *= totalMass / density / volumes[i];
		return res;
	}

	// porosity is defined
	if (hasPorosity)
	{
		CDenseMDMatrix distr = m_phases.at(EPhase::SOLID)->MDDistr()->GetDistribution(_time, DISTR_COMPOUNDS, DISTR_SIZE, DISTR_PART_POROSITY);

		// filter by compounds
		if (activeCompounds.size() != m_compounds.size())
		{
			// TODO: remove cast
			std::vector<size_t> classes = vector_cast<size_t>(distr.GetClasses());
			classes[0] = _compoundKeys.size(); // reduce number of compounds
			CDenseMDMatrix tempDistr{ distr.GetDimensions(), vector_cast<unsigned>(classes) };
			for (size_t iCompound = 0; iCompound < activeCompounds.size(); ++iCompound)
				for (size_t iSize = 0; iSize < nSizeClasses; ++iSize)
				{
					std::vector<double> vec = distr.GetVectorValue(DISTR_COMPOUNDS, static_cast<unsigned>(CompoundIndex(activeCompounds[iCompound])), DISTR_SIZE, static_cast<unsigned>(iSize), DISTR_PART_POROSITY);
					tempDistr.SetVectorValue(DISTR_COMPOUNDS, static_cast<unsigned>(iCompound), DISTR_SIZE, static_cast<unsigned>(iSize), vec);
				}
			tempDistr.Normalize();
			distr = tempDistr;
		}

		const size_t nPorosityClasses = m_grid->GetClassesByDistr(DISTR_PART_POROSITY);
		const std::vector<double> porosities = m_grid->GetClassMeansByDistr(DISTR_PART_POROSITY);

		// calculate distribution
		std::vector<double> res(nSizeClasses);
		for (size_t iCompound = 0; iCompound < activeCompounds.size(); ++iCompound)
		{
			std::vector<double> vec(nSizeClasses);
			const double density = GetCompoundTPProperty(_time, activeCompounds[iCompound], DENSITY);
			if (density == 0.0) return std::vector<double>(nSizeClasses, 0.0);
			for (size_t iSize = 0; iSize < nSizeClasses; ++iSize)
			{
				for (size_t iPorosity = 0; iPorosity < nPorosityClasses; ++iPorosity)
				{
					const double fraction = distr.GetValue(DISTR_COMPOUNDS, static_cast<unsigned>(iCompound), DISTR_SIZE, static_cast<unsigned>(iSize), DISTR_PART_POROSITY, static_cast<unsigned>(iPorosity));
					vec[iSize] += fraction * totalMass / density * (1 - porosities[iPorosity]);
				}
				if (volumes[iSize] != 0.0)
					vec[iSize] /= volumes[iSize];
			}
			AddVectors(res, vec, res);
		}
		return res;
	}

	// several compounds without porosity
	{
		CMatrix2D distr = m_phases.at(EPhase::SOLID)->MDDistr()->GetDistribution(_time, DISTR_COMPOUNDS, DISTR_SIZE);

		// filter by compounds
		if (activeCompounds.size() != m_compounds.size())
		{
			CMatrix2D tempDistr{ distr.Rows(), distr.Cols() - 1 };
			for (size_t i = 0; i < activeCompounds.size(); ++i)
				tempDistr.SetRow(i, distr.GetRow(CompoundIndex(activeCompounds[i])));
			tempDistr.Normalize();
			distr = tempDistr;
		}
		distr *= totalMass;

		// calculate distribution
		std::vector<double> res(nSizeClasses);
		for (size_t iComp = 0; iComp < activeCompounds.size(); ++iComp)
		{
			const double density = GetCompoundTPProperty(_time, activeCompounds[iComp], DENSITY);
			if (density == 0.0) return std::vector<double>(nSizeClasses, 0.0);
			for (size_t iSize = 0; iSize < nSizeClasses; ++iSize)
				if (volumes[iSize] != 0.0)
					res[iSize] += distr[iComp][iSize] / density / volumes[iSize];
		}
		return res;
	}
}

std::vector<double> CBaseStream::GetTimePointsForInterval(double _timeBeg, double _timeEnd, bool _inclusive) const
{
	return _inclusive ? GetTimePointsClosed(_timeBeg, _timeEnd) : GetTimePoints(_timeBeg, _timeEnd);
}

double CBaseStream::GetCompoundPhaseFraction(double _time, const std::string& _compound, unsigned _soa) const
{
	return GetCompoundFraction(_time, _compound, PhaseSOA2EPhase(_soa));
}

double CBaseStream::GetCompoundPhaseFraction(double _time, unsigned _index, unsigned _soa) const
{
	if (_index >= m_compounds.size()) return {};

	return GetCompoundFraction(_time, m_compounds[_index], PhaseSOA2EPhase(_soa));
}

void CBaseStream::SetCompoundPhaseFraction(double _time, const std::string& _compound, unsigned _soa, double _value)
{
	SetCompoundFraction(_time, _compound, PhaseSOA2EPhase(_soa), _value);
}

double CBaseStream::GetCompoundConstant(const std::string& _compound, unsigned _key) const
{
	return GetCompoundConstProperty(_compound, static_cast<ECompoundConstProperties>(_key));
}

double CBaseStream::GetPhaseMass(double _time, unsigned _soa) const
{
	return GetPhaseMass(_time, PhaseSOA2EPhase(_soa));
}

void CBaseStream::SetPhaseMass(double _time, unsigned _soa, double _value)
{
	SetPhaseMass(_time, PhaseSOA2EPhase(_soa), _value);
}

void CBaseStream::AddStream_Base(const CBaseStream& _source, double _time)
{
	Add(_time, _source);
}

double CBaseStream::GetSinglePhaseProp(double _time, unsigned _property, unsigned _soa)
{
	if (_property == PHASE_FRACTION || _property == FRACTION)
		return GetPhaseFraction(_time, PhaseSOA2EPhase(_soa));
	if (_property == FLOW)
		return GetPhaseMass(_time, PhaseSOA2EPhase(_soa));
	if (_property == TEMPERATURE)
		return GetPhaseProperty(_time, PhaseSOA2EPhase(_soa), EOverall::OVERALL_TEMPERATURE);
	if (_property == PRESSURE)
		return GetPhaseProperty(_time, PhaseSOA2EPhase(_soa), EOverall::OVERALL_PRESSURE);
	if (100 <= _property && _property <= 199)
		return GetPhaseProperty(_time, PhaseSOA2EPhase(_soa), static_cast<ECompoundConstProperties>(_property));
	if (200 <= _property && _property <= 299)
		return GetPhaseProperty(_time, PhaseSOA2EPhase(_soa), static_cast<ECompoundTPProperties>(_property));
	return {};
}

double CBaseStream::GetPhaseTPDProp(double _time, unsigned _property, unsigned _soa)
{
	return GetPhaseProperty(_time, PhaseSOA2EPhase(_soa), static_cast<ECompoundTPProperties>(_property));
}

void CBaseStream::SetSinglePhaseProp(double _time, unsigned _property, unsigned _soa, double _value)
{
	if (_property == PHASE_FRACTION || _property == FRACTION)
		SetPhaseFraction(_time, PhaseSOA2EPhase(_soa), _value);
	else if (_property == FLOW)
		SetPhaseMass(_time, PhaseSOA2EPhase(_soa), _value);
}

EPhase CBaseStream::PhaseSOA2EPhase(unsigned _soa)
{
	switch (_soa)
	{
	case SOA_SOLID:		return EPhase::SOLID;
	case SOA_LIQUID:	return EPhase::LIQUID;
	case SOA_VAPOR:		return EPhase::VAPOR;
	default:			return EPhase::UNDEFINED;
	}
}

