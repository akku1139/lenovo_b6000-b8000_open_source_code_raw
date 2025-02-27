/**
 * Copyright (C) 2008 Torch Mobile Inc. All rights reserved. (http://www.torchmobile.com/)
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef WMLTaskElement_h
#define WMLTaskElement_h

#if ENABLE(WML)
#include "WMLElement.h"

#include <wtf/Vector.h>

namespace WebCore {

class WMLPageState;
class WMLSetvarElement;

class WMLTaskElement : public WMLElement {
public:
    virtual bool isWMLTaskElement() const { return true; }

    virtual InsertionNotificationRequest insertedInto(ContainerNode*) OVERRIDE;
    virtual void removedFrom(ContainerNode*) OVERRIDE;
    virtual void executeTask() = 0;

    void registerVariableSetter(WMLSetvarElement*);
    void deregisterVariableSetter(WMLSetvarElement*);

    /// M: Add href for go element.
    virtual KURL href() const { return KURL(); }

protected:
    WMLTaskElement(const QualifiedName& tagName, Document*);
    virtual ~WMLTaskElement();

    void storeVariableState(WMLPageState*);

private:
    Vector<WMLSetvarElement*> m_variableSetterElements;
};

}

#endif
#endif
