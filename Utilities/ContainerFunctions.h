#pragma once

#include <algorithm>
#include <map>
#include <vector>

// Checks whether the vector contains a specified element.
template<typename T>
bool VectorContains(const std::vector<T>& _vec, T _value)
{
	return std::find(_vec.begin(), _vec.end(), _value) != _vec.end();
}

// Returns index of a specified element or -1 if it does not exist.
template<typename T>
size_t VectorFind(const std::vector<T>& _vec, T _val)
{
	for (size_t i = 0; i < _vec.size(); ++i)
		if (_vec[i] == _val)
			return i;
	return -1;
}

// Return the lowest element in vector.
template<typename T>
T VectorMin(const std::vector<T>& _vec)
{
	if (_vec.empty()) return T{};
	return *std::min_element(_vec.begin(), _vec.end());
}

// Return the greatest element in vector.
template<typename T>
T VectorMax(const std::vector<T>& _vec)
{
	if (_vec.empty()) return T{};
	return *std::max_element(_vec.begin(), _vec.end());
}

// Removes all elements of the vector that satisfy the given predicate.
template<typename T, typename FUN>
void VectorDelete(std::vector<T>& _vec, const FUN& _fun)
{
	_vec.erase(std::remove_if(_vec.begin(), _vec.end(), _fun), _vec.end());
}

// Removes an element with the given index if it does exist.
template<typename T>
void VectorDelete(std::vector<T>& _vec, T _value)
{
	_vec.erase(std::remove(_vec.begin(), _vec.end(), _value), _vec.end());
}

// Removes an element with the given index if it does exist.
template<typename T>
void VectorDelete(std::vector<T>& _vec, size_t _index)
{
	if (_index < _vec.size())
		_vec.erase(_vec.begin() + _index);
}

// Adds two equally sized vectors element-wise and writes results to the third one.
template<typename T>
void AddVectors(const std::vector<T>& _vec1, const std::vector<T>& _vec2, std::vector<T>& _res)
{
	std::transform(_vec1.begin(), _vec1.end(), _vec2.begin(), _res.begin(), std::plus<>());
}

// Checks whether the map contains a specified key.
template<typename K, typename V>
bool MapContainsKey(const std::map<K, V>& _map, K _key)
{
	return _map.find(_key) != _map.end();
}

// Checks whether the map contains a specified value.
template<typename K, typename V>
bool MapContainsValue(const std::map<K, V>& _map, V _value)
{
	for (const auto& p : _map)
		if (p.second == _value)
			return true;
	return false;
}

// Returns all keys defined in the map.
template<typename K, typename V>
std::vector<K> MapKeys(const std::map<K, V>& _map)
{
	std::vector<K> keys;
	keys.reserve(_map.size());
	for (auto const& entry : _map)
		keys.push_back(entry.first);
	return keys;
}

// Calculates union of two sorted vectors.
template<typename T>
void VectorsUnionSorted(const std::vector<T>& _v1, const std::vector<T>& _v2, std::vector<T>& _res)
{
	_res.resize(_v1.size() + _v2.size());
	_res.resize(std::set_union(_v1.begin(), _v1.end(), _v2.begin(), _v2.end(), _res.begin()) - _res.begin());
}

// Calculates and returns union of two sorted vectors.
template<typename T>
std::vector<T> VectorsUnionSorted(const std::vector<T>& _v1, const std::vector<T>& _v2)
{
	std::vector<T> res;
	VectorsUnionSorted(_v1, _v2, res);
	return res;
}
