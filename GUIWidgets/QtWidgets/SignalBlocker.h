/* Copyright (c) 2020, Dyssol Development Team. All rights reserved. This file is part of Dyssol. See LICENSE file for license information. */

#pragma once

#include <QObject>

class CSignalBlocker
{
private:
	std::vector<QObject*> m_objects;
	std::vector<bool> m_flags;
public:
	CSignalBlocker(QObject* _object);
	CSignalBlocker(std::initializer_list<QObject*> _objects);
	~CSignalBlocker();

	CSignalBlocker(const CSignalBlocker& _other) = delete;
	CSignalBlocker(CSignalBlocker&& _other) noexcept = delete;
	CSignalBlocker& operator=(const CSignalBlocker& _other) = delete;
	CSignalBlocker& operator=(CSignalBlocker&& _other) noexcept = delete;

	void Unblock();
};
