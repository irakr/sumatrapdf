/* Copyright 2015 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "BaseUtil.h"
#include "ScopedWin.h"
#include "WinUtil.h"

#include "BaseEngine.h"
#include "PdfEngine.h"

#include "FilterBase.h"
#include "PdfFilter.h"
#include "CPdfFilter.h"

VOID CPdfFilter::CleanUp() {
    if (m_pdfEngine) {
        delete m_pdfEngine;
        m_pdfEngine = nullptr;
    }
    m_state = STATE_PDF_END;
}

HRESULT CPdfFilter::OnInit() {
    CleanUp();

    // TODO: PdfEngine::CreateFromStream never returns with
    //       m_pStream instead of a clone - why?

    // load content of PDF document into a seekable stream
    HRESULT res;
    size_t len;
    void* data = GetDataFromStream(m_pStream, &len, &res);
    if (!data)
        return res;

    ScopedComPtr<IStream> stream(CreateStreamFromData(data, len));
    free(data);
    if (!stream)
        return E_FAIL;

    m_pdfEngine = PdfEngine::CreateFromStream(stream);
    if (!m_pdfEngine)
        return E_FAIL;

    m_state = STATE_PDF_START;
    m_iPageNo = 0;
    return S_OK;
}

// copied from SumatraProperties.cpp
static bool PdfDateParse(const WCHAR* pdfDate, SYSTEMTIME* timeOut) {
    ZeroMemory(timeOut, sizeof(SYSTEMTIME));
    // "D:" at the beginning is optional
    if (str::StartsWith(pdfDate, L"D:"))
        pdfDate += 2;
    return str::Parse(pdfDate,
                      L"%4d%2d%2d"
                      L"%2d%2d%2d",
                      &timeOut->wYear, &timeOut->wMonth, &timeOut->wDay, &timeOut->wHour, &timeOut->wMinute,
                      &timeOut->wSecond) != nullptr;
    // don't bother about the day of week, we won't display it anyway
}

HRESULT CPdfFilter::GetNextChunkValue(CChunkValue& chunkValue) {
    AutoFreeW str;

    switch (m_state) {
        case STATE_PDF_START:
            m_state = STATE_PDF_AUTHOR;
            chunkValue.SetTextValue(PKEY_PerceivedType, L"document");
            return S_OK;

        case STATE_PDF_AUTHOR:
            m_state = STATE_PDF_TITLE;
            str.Set(m_pdfEngine->GetProperty(DocumentProperty::Author));
            if (!str::IsEmpty(str.Get())) {
                chunkValue.SetTextValue(PKEY_Author, str);
                return S_OK;
            }
            // fall through

        case STATE_PDF_TITLE:
            m_state = STATE_PDF_DATE;
            str.Set(m_pdfEngine->GetProperty(DocumentProperty::Title));
            if (!str)
                str.Set(m_pdfEngine->GetProperty(DocumentProperty::Subject));
            if (!str::IsEmpty(str.Get())) {
                chunkValue.SetTextValue(PKEY_Title, str);
                return S_OK;
            }
            // fall through

        case STATE_PDF_DATE:
            m_state = STATE_PDF_CONTENT;
            str.Set(m_pdfEngine->GetProperty(DocumentProperty::ModificationDate));
            if (!str)
                str.Set(m_pdfEngine->GetProperty(DocumentProperty::CreationDate));
            if (!str::IsEmpty(str.Get())) {
                SYSTEMTIME systime;
                FILETIME filetime;
                if (PdfDateParse(str, &systime) && SystemTimeToFileTime(&systime, &filetime)) {
                    chunkValue.SetFileTimeValue(PKEY_ItemDate, filetime);
                    return S_OK;
                }
            }
            // fall through

        case STATE_PDF_CONTENT:
            while (++m_iPageNo <= m_pdfEngine->PageCount()) {
                str.Set(m_pdfEngine->ExtractPageText(m_iPageNo, L"\r\n"));
                if (str::IsEmpty(str.Get()))
                    continue;
                chunkValue.SetTextValue(PKEY_Search_Contents, str, CHUNK_TEXT);
                return S_OK;
            }
            m_state = STATE_PDF_END;
            // fall through

        case STATE_PDF_END:
        default:
            return FILTER_E_END_OF_CHUNKS;
    }
}
