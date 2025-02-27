/**
 * Copyright (C) 2009 Torch Mobile Inc. All rights reserved. (http://www.torchmobile.com/)
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

#ifndef WMLFormControlElement_h
#define WMLFormControlElement_h

#if ENABLE(WML)
#include "WMLElement.h"

namespace WebCore {

class WMLFormControlElement : public WMLElement {
public:
    static PassRefPtr<WMLFormControlElement> create(const QualifiedName&, Document*);


    virtual ~WMLFormControlElement();

    virtual bool isFormControlElement() const { return true; }
    virtual bool isReadOnlyFormControl() const { return false; }
    virtual bool isTextFormControl() const { return false; }

    virtual bool formControlValueMatchesRenderer() const { return m_valueMatchesRenderer; }
    virtual void setFormControlValueMatchesRenderer(bool b = true) { m_valueMatchesRenderer = b; }

    virtual bool supportsFocus() const;
    virtual bool isFocusable() const;

    virtual void attach(const AttachContext& = AttachContext()) OVERRIDE;
    virtual void recalcStyle(StyleChange);

    /// M: @{
    virtual bool wasChangedSinceLastFormControlChangeEvent() const;
    virtual void setChangedSinceLastFormControlChangeEvent(bool);
    virtual void dispatchFormControlChangeEvent();
    /// @}

protected:
    WMLFormControlElement(const QualifiedName&, Document*);

    /// M: @{
    virtual void didRecalcStyle(StyleChange) OVERRIDE;
    /// @}

private:
    bool m_valueMatchesRenderer;
    /// M: @{
    bool m_wasChangedSinceLastFormControlChangeEvent : 1;
    /// @}
};

}

#endif
#endif
