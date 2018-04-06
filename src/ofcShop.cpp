/******************************************************************************
 *
 * Project:  oesenc_pi
 * Purpose:  oesenc_pi Plugin core
 * Author:   David Register
 *
 ***************************************************************************
 *   Copyright (C) 2018 by David S. Register                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************
 */


#include "wx/wxprec.h"

#ifndef  WX_PRECOMP
  #include "wx/wx.h"
#endif //precompiled headers

#include <wx/fileconf.h>
#include <wx/uri.h>
#include "wx/tokenzr.h"
#include <wx/dir.h>
#include "ofcShop.h"
#include "ocpn_plugin.h"
#include "wxcurl/wx/curl/http.h"
#include "wxcurl/wx/curl/thread.h"
#include <tinyxml.h>
#include "wx/wfstream.h"
#include <wx/zipstrm.h>
#include <memory>
#include "base64.h"
#include "xtr1_inStream.h"
#include "ofc_pi.h"
#include "version.h"

#include <wx/arrimpl.cpp> 
WX_DEFINE_OBJARRAY(ArrayOfCharts);
WX_DEFINE_OBJARRAY(ArrayOfChartPanels);

WX_DECLARE_STRING_HASH_MAP( int, ProdSKUIndexHash );

//  Static variables
ArrayOfCharts g_ChartArray;

int g_timeout_secs = 5;

wxArrayString g_systemNameChoiceArray;
wxArrayString g_systemNameServerArray;

extern int g_admin;
extern wxString g_loginUser;
extern wxString g_PrivateDataDir;
wxString  g_loginPass;
wxString g_dlStatPrefix;
extern wxString  g_versionString;

shopPanel *g_shopPanel;
OESENC_CURL_EvtHandler *g_CurlEventHandler;
wxCurlDownloadThread *g_curlDownloadThread;
wxFFileOutputStream *downloadOutStream;
bool g_chartListUpdatedOK;
wxString g_statusOverride;
wxString g_lastInstallDir;

int g_downloadChainIdentifier;

#define ID_CMD_BUTTON_INSTALL 7783
#define ID_CMD_BUTTON_INSTALL_CHAIN 7784
#define ID_CMD_BUTTON_INDEX_CHAIN 7785
#define ID_CMD_BUTTON_DOWNLOADLIST_CHAIN 7786
#define ID_CMD_BUTTON_DOWNLOADLIST_PROC 7787

// Private class implementations

size_t wxcurl_string_write_UTF8(char* ptr, size_t size, size_t nmemb, void* pcharbuf)
{
    size_t iRealSize = size * nmemb;
    wxCharBuffer* pStr = (wxCharBuffer*) pcharbuf;
    
    
    if(pStr)
    {
#ifdef __WXMSW__        
        wxString str1a = wxString(*pStr);
        wxString str2 = wxString((const char*)ptr, wxConvUTF8, iRealSize);
        *pStr = (str1a + str2).mb_str();
#else        
        wxString str = wxString(*pStr, wxConvUTF8) + wxString((const char*)ptr, wxConvUTF8, iRealSize);
        *pStr = str.mb_str(wxConvUTF8);
        
/* arm testing       
        wxString str1a = wxString(*pStr, wxConvUTF8);
        wxString str2 = wxString((const char*)ptr, wxConvUTF8, iRealSize);
        *pStr = (str1a + str2).mb_str(wxConvUTF8);
        
//        char *v = pStr->data();
//        printf("concat(new): %s\n\n\n", (char *)v);
        
//        printf("LOGGING...\n\n\n\n");
//        wxLogMessage(_T("str1a: ") + str1a);
//        wxLogMessage(_T("str2: ") + str2);
*/        
#endif        
    }
    
    
    return iRealSize;
}

class wxCurlHTTPNoZIP : public wxCurlHTTP
{
public:
    wxCurlHTTPNoZIP(const wxString& szURL = wxEmptyString,
               const wxString& szUserName = wxEmptyString,
               const wxString& szPassword = wxEmptyString,
               wxEvtHandler* pEvtHandler = NULL, int id = wxID_ANY,
               long flags = wxCURL_DEFAULT_FLAGS);
    
   ~wxCurlHTTPNoZIP();
    
   bool Post(wxInputStream& buffer, const wxString& szRemoteFile /*= wxEmptyString*/);
   bool Post(const char* buffer, size_t size, const wxString& szRemoteFile /*= wxEmptyString*/);
   std::string GetResponseBody() const;
protected:
    void SetCurlHandleToDefaults(const wxString& relativeURL);
    
};

wxCurlHTTPNoZIP::wxCurlHTTPNoZIP(const wxString& szURL /*= wxEmptyString*/, 
                       const wxString& szUserName /*= wxEmptyString*/, 
                       const wxString& szPassword /*= wxEmptyString*/, 
                       wxEvtHandler* pEvtHandler /*= NULL*/, 
                       int id /*= wxID_ANY*/,
                       long flags /*= wxCURL_DEFAULT_FLAGS*/)
: wxCurlHTTP(szURL, szUserName, szPassword, pEvtHandler, id, flags)

{
}

wxCurlHTTPNoZIP::~wxCurlHTTPNoZIP()
{
    ResetPostData();
}

void wxCurlHTTPNoZIP::SetCurlHandleToDefaults(const wxString& relativeURL)
{
    wxCurlBase::SetCurlHandleToDefaults(relativeURL);
    
    SetOpt(CURLOPT_ENCODING, "identity");               // No encoding, plain ASCII
    
    if(m_bUseCookies)
    {
        SetStringOpt(CURLOPT_COOKIEJAR, m_szCookieFile);
    }
}

bool wxCurlHTTPNoZIP::Post(const char* buffer, size_t size, const wxString& szRemoteFile /*= wxEmptyString*/)
{
    wxMemoryInputStream inStream(buffer, size);
    
    return Post(inStream, szRemoteFile);
}

bool wxCurlHTTPNoZIP::Post(wxInputStream& buffer, const wxString& szRemoteFile /*= wxEmptyString*/)
{
    curl_off_t iSize = 0;
    
    if(m_pCURL && buffer.IsOk())
    {
        SetCurlHandleToDefaults(szRemoteFile);
        
        curl_easy_setopt(m_pCURL, CURLOPT_SSL_VERIFYPEER, FALSE);
        
        SetHeaders();
        iSize = buffer.GetSize();
        
        if(iSize == (~(ssize_t)0))      // wxCurlHTTP does not know how to upload unknown length streams.
            return false;
        
        SetOpt(CURLOPT_POST, TRUE);
        SetOpt(CURLOPT_POSTFIELDSIZE_LARGE, iSize);
        SetStreamReadFunction(buffer);
        
        //  Use a private data write trap function to handle UTF8 content
        //SetStringWriteFunction(m_szResponseBody);
        SetOpt(CURLOPT_WRITEFUNCTION, wxcurl_string_write_UTF8);         // private function
        SetOpt(CURLOPT_WRITEDATA, (void*)&m_szResponseBody);
        
        if(Perform())
        {
            ResetHeaders();
            return IsResponseOk();
        }
    }
    
    return false;
}

std::string wxCurlHTTPNoZIP::GetResponseBody() const
{
#ifndef ARMHF
     wxString s = wxString((const char *)m_szResponseBody, wxConvLibc);
     return std::string(s.mb_str());

#else    
    return std::string((const char *)m_szResponseBody);
#endif
    
}

// itemChart
//------------------------------------------------------------------------------------------



itemChart::itemChart( wxString &product_sku, int index) {
    productSKU = product_sku;
    indexSKU = index; 
    m_status = STAT_UNKNOWN;
}


bool itemChart::isMatch(itemChart *thatItemChart)
{
    return ( (productSKU == thatItemChart->productSKU) && (indexSKU == thatItemChart->indexSKU) );
}


// void itemChart::setDownloadPath(int slot, wxString path) {
//     if (slot == 0)
//         fileDownloadPath0 = path;
//     else if (slot == 1)
//         fileDownloadPath1 = path;
// }

// wxString itemChart::getDownloadPath(int slot) {
//     if (slot == 0)
//         return fileDownloadPath0;
//     else if (slot == 1)
//         return fileDownloadPath1;
//     else
//         return _T("");
// }

bool itemChart::isChartsetAssignedToMe(wxString systemName){
    return device_ok;
}


bool itemChart::isChartsetFullyAssigned() {
    return bActivated;
}

bool itemChart::isChartsetExpired() {
    
    bool bExp = false;
//     if (statusID0.IsSameAs("expired") || statusID1.IsSameAs("expired")) {
//         bExp = true;
//     }
    return bExp;
}

bool itemChart::isChartsetDontShow()
{
    if(isChartsetFullyAssigned() && !isChartsetAssignedToMe(wxEmptyString))
        return true;
    
    else if(isChartsetExpired() && !isChartsetAssignedToMe(wxEmptyString))
        return true;
    
    else
        return false;
}
    
    
//  Current status can be one of:
/*
 *      1.  Available for Installation.
 *      2.  Installed, Up-to-date.
 *      3.  Installed, Update available.
 *      4.  Expired.
 */        

int itemChart::getChartStatus()
{
    if(!g_chartListUpdatedOK){
        m_status = STAT_NEED_REFRESH;
        return m_status;
    }

    if(isChartsetExpired()){
        m_status = STAT_EXPIRED;
        return m_status;
    }
    
//     if(!isChartsetAssignedToMe( g_systemName )){
//         m_status = STAT_PURCHASED;
//         return m_status;
//     }

    if(!bActivated){
        m_status = STAT_REQUESTABLE;
        return m_status;
    }
    else if(installLocation.Length()){
        m_status = STAT_CURRENT;
    }
    else{
        m_status = STAT_READY_DOWNLOAD;
        return m_status;
    }

#if 0    
    
    // We know that chart is assigned to me, so one of the sysIDx fields will match
    wxString cStat = statusID0;
    if(sysID1.IsSameAs(g_systemName))
        cStat = statusID1;
        
    if(cStat.IsSameAs(_T("requestable"))){
        m_status = STAT_REQUESTABLE;
        return m_status;
    }

    if(cStat.IsSameAs(_T("processing"))){
        m_status = STAT_PREPARING;
        return m_status;
    }

    if(cStat.IsSameAs(_T("download"))){
        m_status = STAT_READY_DOWNLOAD;
        
        if(sysID0.IsSameAs(g_systemName)){
            if(  (installLocation0.Length() > 0) && (installedFileDownloadPath0.Length() > 0) ){
                m_status = STAT_CURRENT;
                if(!installedEdition0.IsSameAs(currentChartEdition)){
                    m_status = STAT_STALE;
                }
            }
        }
        else if(sysID1.IsSameAs(g_systemName)){
            if(  (installLocation1.Length() > 0) && (installedFileDownloadPath1.Length() > 0) ){
                m_status = STAT_CURRENT;
                if(!installedEdition1.IsSameAs(currentChartEdition)){
                    m_status = STAT_STALE;
                }
                
            }
        }
    }
#endif
     
    return m_status;
    
}
wxString itemChart::getStatusString()
{
    getChartStatus();
    
    wxString sret;
    
    switch(m_status){
        
        case STAT_UNKNOWN:
            break;
            
        case STAT_PURCHASED:
            sret = _("Available.");
            break;
            
        case STAT_CURRENT:
            sret = _("Installed, Up-to-date.");
            break;
            
        case STAT_STALE:
            sret = _("Installed, Update available.");
            break;
            
        case STAT_EXPIRED:
        case STAT_EXPIRED_MINE:
            sret = _("Expired.");
            break;
            
        case STAT_READY_DOWNLOAD:
            sret = _("Ready for download/install.");
            break;
            
        case STAT_NEED_REFRESH:
            sret = _("Please update Chart List.");
            break;

        case STAT_REQUESTABLE:
            sret = _("Ready for Activation Request.");
            break;
            
        default:
            break;
    }
    
    return sret;
    

}

wxBitmap& itemChart::GetChartThumbnail(int size)
{
    if(!m_ChartImage.IsOk()){
        // Look for cached copy
        wxString fileKey = _T("ChartImage-");
        fileKey += productSKU;
        fileKey += _T(".jpg");
 
        wxString file = g_PrivateDataDir + fileKey;
        if(::wxFileExists(file)){
            m_ChartImage = wxImage( file, wxBITMAP_TYPE_ANY);
        }
        else{
            if(g_chartListUpdatedOK && thumbnailURL.Length()){  // Do not access network until after first "getList"
                wxCurlHTTP get;
                get.SetOpt(CURLOPT_TIMEOUT, g_timeout_secs);
                /*bool getResult = */get.Get(file, thumbnailURL);

            // get the response code of the server
                int iResponseCode;
                get.GetInfo(CURLINFO_RESPONSE_CODE, &iResponseCode);
            
                if(iResponseCode == 200){
                    if(::wxFileExists(file)){
                        m_ChartImage = wxImage( file, wxBITMAP_TYPE_ANY);
                    }
                }
            }
        }
    }
    
    if(m_ChartImage.IsOk()){
        int scaledHeight = size;
        int scaledWidth = m_ChartImage.GetWidth() * scaledHeight / m_ChartImage.GetHeight();
        wxImage scaledImage = m_ChartImage.Rescale(scaledWidth, scaledHeight);
        m_bm = wxBitmap(scaledImage);
        
        return m_bm;
    }
    else{
        wxImage img(size, size);
        unsigned char *data = img.GetData();
        for(int i=0 ; i < size * size * 3 ; i++)
            data[i] = 200;
        
        m_bm = wxBitmap(img);   // Grey bitmap
        return m_bm;
    }
    
}




//Utility Functions

//  Search g_ChartArray for chart having specified parameters
int findOrderRefChartId(wxString &SKU, int index)
{
    for(unsigned int i = 0 ; i < g_ChartArray.GetCount() ; i++){
        if(g_ChartArray.Item(i)->productSKU.IsSameAs(SKU)
            && (g_ChartArray.Item(i)->indexSKU == index) ){
                return (i);
            }
    }
    return -1;
}


void loadShopConfig()
{
    //    Get a pointer to the opencpn configuration object
    wxFileConfig *pConf = GetOCPNConfigObject();
    
    if( pConf ) {
        pConf->SetPath( _T("/PlugIns/ofc_pi") );
        
        pConf->Read( _T("loginUser"), &g_loginUser);
        pConf->Read( _T("loginPass"), &g_loginPass);
        pConf->Read( _T("lastInstallDir"), &g_lastInstallDir);
        
        pConf->Read( _T("ADMIN"), &g_admin);
                
        pConf->SetPath ( _T ( "/PlugIns/ofc_pi/charts" ) );
        wxString strk;
        wxString kval;
        long dummyval;
        bool bContk = pConf->GetFirstEntry( strk, dummyval );
        while( bContk ) {
            pConf->Read( strk, &kval );
            
            // Parse the key
            // Remove the last two characters (the SKU index)
            wxString SKU = strk.Mid(0, strk.Length() - 2);
            wxString sindex = strk.Right(1);
            long lidx = 0;
            sindex.ToLong(&lidx);
            
            // Add a chart if necessary
            int index = -1; //findOrderRefChartId(order, id, qty);
            itemChart *pItem;
            if(index < 0){
                pItem = new itemChart(SKU, lidx);
                g_ChartArray.Add(pItem);
            }
            else
                pItem = g_ChartArray.Item(index);

            // Parse the value
            wxStringTokenizer tkz( kval, _T(";") );
            wxString name = tkz.GetNextToken();
            wxString installDir = tkz.GetNextToken();
            
            pItem->productName = name;
            if(pItem->chartInstallLocnFull.IsEmpty())   pItem->chartInstallLocnFull = installDir;
            
            //  Extract the parent of the full location
            wxFileName fn(installDir);
            pItem->installLocation = fn.GetPath();
           
            bContk = pConf->GetNextEntry( strk, dummyval );
        }
    }
}

void saveShopConfig()
{
    wxFileConfig *pConf = GetOCPNConfigObject();
        
   if( pConf ) {
      pConf->SetPath( _T("/PlugIns/ofc_pi") );
            
      if(g_admin){
          pConf->Write( _T("loginUser"), g_loginUser);
          pConf->Write( _T("loginPass"), g_loginPass);
      }
      
      pConf->Write( _T("lastInstallDir"), g_lastInstallDir);
      
      pConf->DeleteGroup( _T("/PlugIns/ofc_pi/charts") );
      pConf->SetPath( _T("/PlugIns/ofc_pi/charts") );
      
      for(unsigned int i = 0 ; i < g_ChartArray.GetCount() ; i++){
          itemChart *chart = g_ChartArray.Item(i);
          if(chart->bActivated && chart->device_ok){            // Mine...
            wxString idx;
            idx.Printf(_T("%d"), chart->indexSKU);
            wxString key = chart->productSKU + _T("-") + idx;
            
            wxString val = chart->productName + _T(";");
            val += chart->chartInstallLocnFull + _T(";");
            pConf->Write( key, val );
          }
      }
   }
}

            
int checkResult(wxString &result, bool bShowErrorDialog = true)
{
    if(g_shopPanel){
        g_shopPanel->getInProcessGuage()->Stop();
    }
    
    long dresult;
    if(result.ToLong(&dresult)){
        if(dresult == 1)
            return 0;
        else{
            if(bShowErrorDialog){
                wxString msg = _("o-charts API error code: ");
                wxString msg1;
                msg1.Printf(_T("{%ld}\n\n"), dresult);
                msg += msg1;
                switch(dresult){
                    case 3:
                        msg += _("Invalid user/email name or password.");
                        break;
                    default:    
                        msg += _("Check your configuration and try again.");
                        break;
                }
                
                OCPNMessageBox_PlugIn(NULL, msg, _("ofc_pi Message"), wxOK);
            }
            return dresult;
        }
    }
    return 98;
}

int checkResponseCode(int iResponseCode)
{
    if(iResponseCode != 200){
        wxString msg = _("internet communications error code: ");
        wxString msg1;
        msg1.Printf(_T("{%d}\n "), iResponseCode);
        msg += msg1;
        msg += _("Check your connection and try again.");
        OCPNMessageBox_PlugIn(NULL, msg, _("ofc_pi Message"), wxOK);
    }
    
    // The wxCURL library returns "0" as response code,
    // even when it should probably return 404.
    // We will use "99" as a special code here.
    
    if(iResponseCode < 100)
        return 99;
    else
        return iResponseCode;
        
}

bool doLogin()
{
    xtr1Login login(g_shopPanel);
    login.m_UserNameCtl->SetValue(g_loginUser);
    login.m_PasswordCtl->SetValue(g_loginPass);

    login.ShowModal();
    if(!login.GetReturnCode() == 0){
        g_shopPanel->setStatusText( _("Invalid Login."));
        wxYield();
        return false;
    }
    
    g_loginUser = login.m_UserNameCtl->GetValue();
    g_loginPass = login.m_PasswordCtl->GetValue();
    
    return true;
    
#if 0    
    
    wxString url = userURL;
    if(g_admin)
        url = adminURL;
    
    url +=_T("?fc=module&module=occharts&controller=api");
    
    wxString loginParms;
    loginParms += _T("taskId=login");
    loginParms += _T("&username=") + g_loginUser;
    loginParms += _T("&password=") + pass;
    
    wxCurlHTTPNoZIP post;
    post.SetOpt(CURLOPT_TIMEOUT, g_timeout_secs);
    size_t res = post.Post( loginParms.ToAscii(), loginParms.Len(), url );
    
    // get the response code of the server
    int iResponseCode;
    post.GetInfo(CURLINFO_RESPONSE_CODE, &iResponseCode);
    
    if(iResponseCode == 200){
        TiXmlDocument * doc = new TiXmlDocument();
        const char *rr = doc->Parse( post.GetResponseBody().c_str());
        
        wxString queryResult;
        wxString loginKey;
        
        if( res )
        {
            TiXmlElement * root = doc->RootElement();
            if(!root){
                wxString r = _T("50");
                checkResult(r);                              // undetermined error??
                return false;
            }
            
            wxString rootName = wxString::FromUTF8( root->Value() );
            TiXmlNode *child;
            for ( child = root->FirstChild(); child != 0; child = child->NextSibling()){
                wxString s = wxString::FromUTF8(child->Value());
                
                if(!strcmp(child->Value(), "result")){
                    TiXmlNode *childResult = child->FirstChild();
                    queryResult =  wxString::FromUTF8(childResult->Value());
                }
                else if(!strcmp(child->Value(), "key")){
                    TiXmlNode *childResult = child->FirstChild();
                    loginKey =  wxString::FromUTF8(childResult->Value());
                }
            }
        }
        
        if(queryResult == _T("1"))
            g_loginKey = loginKey;
        
        return (checkResult(queryResult) == 0);
    }
    else
        return false;
#endif    
}

/*
<product
product_sku="TD-ST-HG"
product_name="Swedish Marine including Hydrographica charts - 2018 edition"
product_type="Touratel"
expiry="0000-00-00T00:00:00Z"
purchase_url="https://fugawi.com/store/product/TD-ST-HG"
activated="false"
app_id_ok="false"
device_id_ok="false">
</product>
*/

wxString ProcessResponse(std::string body)
{
        TiXmlDocument * doc = new TiXmlDocument();
        doc->Parse( body.c_str());
    
        //doc->Print();
        
        wxString queryResult;
        wxString chartOrder;
        wxString chartPurchase;
        wxString chartExpiration;
        wxString chartID;
        wxString chartEdition;
        wxString chartPublication;
        wxString chartName;
        wxString chartQuantityID;
        wxString chartSlot;
        wxString chartAssignedSystemName;
        wxString chartLastRequested;
        wxString chartState;
        wxString chartLink;
        wxString chartSize;
        wxString chartThumbURL;
        
        wxString product_name;
        wxString product_sku;
        wxString product_type;
        wxString expiry;
        wxString purchase_url;
        wxString activated;
        wxString app_id_ok;
        wxString device_id_ok;
        wxString product_key;
        wxString product_body;
        
        ProdSKUIndexHash psi;           // Product SKU keys, instance counter value
        
            TiXmlElement * root = doc->RootElement();
            if(!root){
                return _T("50");                              // undetermined error??
            }
            //g_ChartArray.Clear();
            
            wxString rootName = wxString::FromUTF8( root->Value() );
            TiXmlNode *child;
            for ( child = root->FirstChild(); child != 0; child = child->NextSibling()){
                wxString s = wxString::FromUTF8(child->Value());
                
                if(!strcmp(child->Value(), "result")){
                    TiXmlNode *childResult = child->FirstChild();
                    queryResult =  wxString::FromUTF8(childResult->Value());
                }
                

                else if(!strcmp(child->Value(), "account")){
                    TiXmlNode *childacct = child->FirstChild();
                    for ( childacct = child->FirstChild(); childacct!= 0; childacct = childacct->NextSibling()){
                        
                        if(!strcmp(childacct->Value(), "product")){
                            TiXmlElement *product = childacct->ToElement();
                            
                            TiXmlNode *productBody64 = childacct->FirstChild();
                            if(productBody64)
                                product_body = productBody64->Value();

#ifdef __WXMSW__
                            product_name = wxString( product->Attribute( "product_name" ) ); // already converted....
#else
                            product_name = wxString( product->Attribute( "product_name" ), wxConvUTF8 );
#endif                            
                            
                            product_sku = wxString( product->Attribute( "product_sku" ), wxConvUTF8 );
                            product_type = wxString( product->Attribute( "product_type" ), wxConvUTF8 );
                            expiry = wxString( product->Attribute( "expiry" ), wxConvUTF8 );
                            purchase_url = wxString( product->Attribute( "purchase_url" ), wxConvUTF8 );
                            activated = wxString( product->Attribute( "activated" ), wxConvUTF8 );
                            app_id_ok = wxString( product->Attribute( "app_id_ok" ), wxConvUTF8 );
                            device_id_ok = wxString( product->Attribute( "device_id_ok" ), wxConvUTF8 );
                            product_key = wxString( product->Attribute( "product_key" ), wxConvUTF8 );
 
                            
                            int index = 0;
                            //  Has this product sku been seen yet?
                            if(psi.find( product_sku ) == psi.end()){           // first one
                                psi[product_sku] = 1;
                                index = 1;
                            }
                            else{
                                index = psi[product_sku];
                                index++;
                                psi[product_sku] = index;
                            }
                        
                            // Process this chart node
//                             itemChart *pItem;
//                             pItem = new itemChart(product_sku, index);
//                             g_ChartArray.Add(pItem);
                            
                            // As identified uniquely by Sku and index....
                            // Does this chart already exist in the table?
                             itemChart *pItem;
                             int indexChart = findOrderRefChartId(product_sku, index);
                             if(indexChart < 0){
                                 pItem = new itemChart(product_sku, index);
                                 g_ChartArray.Add(pItem);
                             }
                             else
                                 pItem = g_ChartArray.Item(indexChart);
                            
                            // Populate in the rest of "item"
                            pItem->productName = product_name;
                            pItem->expDate = expiry;
                            pItem->fileDownloadURL = purchase_url;
                            pItem->productType = product_type;
                            pItem->productKey = product_key;
                            pItem->productBody = product_body;
                            pItem->device_ok = device_id_ok.IsSameAs(_T("TRUE"), false);
                            pItem->bActivated = activated.IsSameAs(_T("TRUE"), false);
                        }
                    }
                    
                
                }
            }
        
        
        return queryResult;
}

    

int getChartList( bool bShowErrorDialogs = true){
    
     validate_server();
     
     xtr1_inStream GK;
     wxString kk = GK.getHK();
     
     if(!kk.Len())
         return 2;

     
    // We query the server for the list of charts associated with our account
    wxString url = _T("https://fugawi.com/GetAccount_v2.xml");
    
    wxString loginParms;
    loginParms = _T("email=");
    loginParms += g_loginUser;
    
    loginParms += _T("&password=");
    loginParms += g_loginPass;
    
    loginParms += _T("&api_key=cb437274b425c92e00ed2ea802959e11d0e2048a");
    loginParms += _T("&app_id=30000");
    loginParms += _T("&device_id=");
    loginParms += kk;

    //wxLogMessage(_T("getChartList Login Parms: ") + loginParms);
    
    wxCurlHTTPNoZIP post;
    //wxCurlHTTP post;
    //post.SetOpt(CURLOPT_TIMEOUT, g_timeout_secs);
    
    /*size_t res = */post.Post( loginParms.ToAscii(), loginParms.Len(), url );
    
    // get the response code of the server
    int iResponseCode;
    post.GetInfo(CURLINFO_RESPONSE_CODE, &iResponseCode);
    
    std::string a = post.GetDetailedErrorString();
    std::string b = post.GetErrorString();
    std::string c = post.GetResponseBody();
    const char *d = c.c_str();
    
    //printf("Code %d\n", iResponseCode);
    
    //printf("%s\n", a.c_str());
    //printf("%s\n", b.c_str());
    //printf("%s\n", c.c_str());
    //printf("%s\n", d);
    
    //printf("%s", post.GetResponseBody().c_str());
    
     //wxString tt(post.GetResponseBody().data(), wxConvUTF8);
     //wxLogMessage( _T("Response: \n") + tt);
    
     if(iResponseCode == 200){
         wxString result = ProcessResponse(post.GetResponseBody());
         return 0;
         
//         return checkResult( result, bShowErrorDialogs );
     }
     else{
         //wxLogMessage(_T("Login Parms: ") + loginParms);
         return iResponseCode; //checkResponseCode(iResponseCode);
     }
}


int doActivate(itemChart *chart)
{
    validate_server();
    
    xtr1_inStream GK;
    wxString kk = GK.getHK();
    
    if(!kk.Len())
        return 1;
    
    wxString msg = _("This action will PERMANENTLY assign the chart:");
    msg += _T("\n        ");
    msg += chart->productName;
    msg += _T("\n\n");
    msg += _("to this system.");
    msg += _T("\n\n");
    msg += _("Proceed?");
    
    int ret = OCPNMessageBox_PlugIn(NULL, msg, _("ofc_pi Message"), wxYES_NO);
     
    if(ret != wxID_YES){
         return 1;
    }

    
    // Activate this chart

    
    wxString url = _T("https://fugawi.com/ActivateProduct.xml");
    
    wxString loginParms;
    loginParms = _T("email=");
    loginParms += g_loginUser;
    
    loginParms += _T("&password=");
    loginParms += g_loginPass;
    
    loginParms += _T("&api_key=cb437274b425c92e00ed2ea802959e11d0e2048a");
    loginParms += _T("&app_id=30000");
    loginParms += _T("&device_id=");
    loginParms += kk;
 
    loginParms += _T("&product_sku=");
    loginParms += chart->productSKU;
    
    wxCurlHTTPNoZIP post;
    post.SetOpt(CURLOPT_TIMEOUT, g_timeout_secs);
    
    post.Post( loginParms.ToAscii(), loginParms.Len(), url );
    
    // get the response code of the server
    int iResponseCode;
    post.GetInfo(CURLINFO_RESPONSE_CODE, &iResponseCode);
    
//     std::string a = post.GetDetailedErrorString();
//     std::string b = post.GetErrorString();
//     std::string c = post.GetResponseBody();
    
    //printf("%s", post.GetResponseBody().c_str());
    
    //wxString tt(post.GetResponseBody().data(), wxConvUTF8);
    //wxLogMessage(tt);
    
    if(iResponseCode == 200){
        wxString result = ProcessResponse(post.GetResponseBody());
        return 0;
        
        //         return checkResult( result, bShowErrorDialogs );
    }
    else
        return iResponseCode; //checkResponseCode(iResponseCode);


}

extern wxString getFPR( bool bCopyToDesktop, bool &bCopyOK);

int doUploadXFPR()
{
    return 0;
}


int doPrepare(oeSencChartPanel *chartPrepare, int slot)
{
    return 0;
}

int doDownload(oeSencChartPanel *chartDownload)
{
    itemChart *chart = chartDownload->m_pChart;

    //  Create a destination file name for the download.
    wxURI uri;
    
    wxString downloadURL = chart->fileDownloadURL;

    uri.Create(downloadURL);
    
    wxString serverFilename = uri.GetPath();
    wxString b = uri.GetServer();
    
    wxFileName fn(serverFilename);
    
    wxString downloadFile = g_PrivateDataDir + fn.GetFullName();
    chart->downloadingFile = downloadFile;
    
    downloadOutStream = new wxFFileOutputStream(downloadFile);
    
    g_curlDownloadThread = new wxCurlDownloadThread(g_CurlEventHandler);
    g_curlDownloadThread->SetURL(downloadURL);
    g_curlDownloadThread->SetOutputStream(downloadOutStream);
    g_curlDownloadThread->Download();
 

    return 0;
}

bool ExtractZipFiles( const wxString& aZipFile, const wxString& aTargetDir, bool aStripPath, wxDateTime aMTime, bool aRemoveZip )
{
    bool ret = true;
    
    std::auto_ptr<wxZipEntry> entry(new wxZipEntry());
    
    do
    {
        //wxLogError(_T("chartdldr_pi: Going to extract '")+aZipFile+_T("'."));
        wxFileInputStream in(aZipFile);
        
        if( !in )
        {
            wxLogError(_T("Can not open file '")+aZipFile+_T("'."));
            ret = false;
            break;
        }
        wxZipInputStream zip(in);
        ret = false;
        
        while( entry.reset(zip.GetNextEntry()), entry.get() != NULL )
        {
            // access meta-data
            wxString name = entry->GetName();
            if( aStripPath )
            {
                wxFileName fn(name);
                /* We can completly replace the entry path */
                //fn.SetPath(aTargetDir);
                //name = fn.GetFullPath();
                /* Or only remove the first dir (eg. ENC_ROOT) */
                if (fn.GetDirCount() > 0)
                    fn.RemoveDir(0);
                name = aTargetDir + wxFileName::GetPathSeparator() + fn.GetFullPath();
            }
            else
            {
                name = aTargetDir + wxFileName::GetPathSeparator() + name;
            }
            
            // read 'zip' to access the entry's data
            if( entry->IsDir() )
            {
                int perm = entry->GetMode();
                if( !wxFileName::Mkdir(name, perm, wxPATH_MKDIR_FULL) )
                {
                    wxLogError(_T("Can not create directory '") + name + _T("'."));
                    ret = false;
                    break;
                }
            }
            else
            {
                if( !zip.OpenEntry(*entry.get()) )
                {
                    wxLogError(_T("Can not open zip entry '") + entry->GetName() + _T("'."));
                    ret = false;
                    break;
                }
                if( !zip.CanRead() )
                {
                    wxLogError(_T("Can not read zip entry '") + entry->GetName() + _T("'."));
                    ret = false;
                    break;
                }
                
                wxFileName fn(name);
                if( !fn.DirExists() )
                {
                    if( !wxFileName::Mkdir(fn.GetPath()) )
                    {
                        wxLogError(_T("Can not create directory '") + fn.GetPath() + _T("'."));
                        ret = false;
                        break;
                    }
                }
                
                wxFileOutputStream file(name);
                
                g_shopPanel->setStatusText( _("Unzipping chart files...") + fn.GetFullName());
                wxYield();
                
                if( !file )
                {
                    wxLogError(_T("Can not create file '")+name+_T("'."));
                    ret = false;
                    break;
                }
                zip.Read(file);
                //fn.SetTimes(&aMTime, &aMTime, &aMTime);
                ret = true;
            }
            
        }
        
    }
    while(false);
    
    if( aRemoveZip )
        wxRemoveFile(aZipFile);
    
    return ret;
}


int doShop(){
    
    loadShopConfig();
   
    //  Do we need an initial login to get the persistent key?
    doLogin();
    saveShopConfig();
    
    getChartList();
    
    return 0;
}


class MyStaticTextCtrl : public wxStaticText {
public:
    MyStaticTextCtrl(wxWindow* parent,
                                       wxWindowID id,
                                       const wxString& label,
                                       const wxPoint& pos,
                                       const wxSize& size = wxDefaultSize,
                                       long style = 0,
                                       const wxString& name= "staticText" ):
                                       wxStaticText(parent,id,label,pos,size,style,name){};
                                       void OnEraseBackGround(wxEraseEvent& event) {};
                                       DECLARE_EVENT_TABLE()
};

BEGIN_EVENT_TABLE(MyStaticTextCtrl,wxStaticText)
EVT_ERASE_BACKGROUND(MyStaticTextCtrl::OnEraseBackGround)
END_EVENT_TABLE()


BEGIN_EVENT_TABLE(oeSencChartPanel, wxPanel)
EVT_PAINT ( oeSencChartPanel::OnPaint )
EVT_ERASE_BACKGROUND(oeSencChartPanel::OnEraseBackground)
END_EVENT_TABLE()

oeSencChartPanel::oeSencChartPanel(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size, itemChart *p_itemChart, shopPanel *pContainer)
:wxPanel(parent, id, pos, size, wxBORDER_NONE)
{
    m_pContainer = pContainer;
    m_pChart = p_itemChart;
    m_bSelected = false;

    int refHeight = GetCharHeight();
    SetMinSize(wxSize(-1, 5 * refHeight));
    m_unselectedHeight = 5 * refHeight;
    
//     wxBoxSizer* itemBoxSizer01 = new wxBoxSizer(wxHORIZONTAL);
//     SetSizer(itemBoxSizer01);
     Connect(wxEVT_LEFT_DOWN, wxMouseEventHandler(oeSencChartPanel::OnChartSelected), NULL, this);
//     
    
}

oeSencChartPanel::~oeSencChartPanel()
{
}

void oeSencChartPanel::OnChartSelected( wxMouseEvent &event )
{
    // Do not allow de-selection by mouse if this chart is busy, i.e. being prepared, or being downloaded 
    if(m_pChart){
       if(g_statusOverride.Length())
           return;
    }
           
    if(!m_bSelected){
        SetSelected( true );
        m_pContainer->SelectChart( this );
    }
    else{
        SetSelected( false );
        m_pContainer->SelectChart( NULL );
    }
}

void oeSencChartPanel::SetSelected( bool selected )
{
    m_bSelected = selected;
    wxColour colour;
    int refHeight = GetCharHeight();
    
    if (selected)
    {
        GetGlobalColor(_T("UIBCK"), &colour);
        m_boxColour = colour;
        SetMinSize(wxSize(-1, 9 * refHeight));
    }
    else
    {
        GetGlobalColor(_T("DILG0"), &colour);
        m_boxColour = colour;
        SetMinSize(wxSize(-1, 5 * refHeight));
    }
    
    Refresh( true );
    
}


extern "C"  DECL_EXP bool GetGlobalColor(wxString colorName, wxColour *pcolour);

void oeSencChartPanel::OnEraseBackground( wxEraseEvent &event )
{
}

wxArrayString splitLine(wxString &line, wxDC &dc, int widthMax)
{
    // Split into two lines...
    wxString line0;
    wxString line1;
    int lenCheck;
    
    unsigned int i = 0;
    unsigned int imax = 0;
    unsigned int iprev = 0;
    bool bsplit = false;
    
    bool done = false;
    while(!done && (i < line.Len() - 1)){
        while(line[i] != ' '){
            i++;
        }
    
        wxString test_string = line.Mid(0, i);
        dc.GetTextExtent(test_string, &lenCheck, NULL);
        if(lenCheck > widthMax){
            done = true;
            imax = iprev;
            bsplit = true;
            break;
        }
        else{
            iprev = i++;
        }
    }

    if(!bsplit)
        imax = line.Len();

    line0 = line.Mid(0, imax);

    if(bsplit && (imax < line.Len() - 1))
        line1 = line.Mid(imax);
    
    wxArrayString retArray;
    retArray.Add(line0);
    retArray.Add(line1);
    
    return retArray;
}


void oeSencChartPanel::OnPaint( wxPaintEvent &event )
{
    int width, height;
    GetSize( &width, &height );
    wxPaintDC dc( this );
 
    //dc.SetBackground(*wxLIGHT_GREY);
    
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(GetBackgroundColour()));
    dc.DrawRectangle(GetVirtualSize());
    
    wxColour c;
    
    wxString nameString = m_pChart->productName;
//     wxString idxs;
//     idxs.Printf(_T("%d"),  m_pChart->indexSKU );
//     nameString += _T(" (") + idxs + _T(")");
    
    if(m_bSelected){
        dc.SetBrush( wxBrush( m_boxColour ) );
        
        GetGlobalColor( _T ( "UITX1" ), &c );
        dc.SetPen( wxPen( wxColor(0xCE, 0xD5, 0xD6), 3 ));
        
        dc.DrawRoundedRectangle( 0, 0, width-1, height-1, height / 10);
        
        int base_offset = height / 10;
        
        // Draw the thumbnail
        int scaledWidth = height;
        
        int scaledHeight = (height - (2 * base_offset)) * 95 / 100;
        wxBitmap &bm = m_pChart->GetChartThumbnail( scaledHeight );
        
        if(bm.IsOk()){
            dc.DrawBitmap(bm, base_offset + 3, base_offset + 3);
        }
        
        wxFont *dFont = GetOCPNScaledFont_PlugIn(_("Dialog"));
        double font_size = dFont->GetPointSize() * 4/3;
        wxFont *qFont = wxTheFontList->FindOrCreateFont( font_size, dFont->GetFamily(), dFont->GetStyle(),  wxFONTWEIGHT_BOLD);
        dc.SetFont( *qFont );
        dc.SetTextForeground(wxColour(0,0,0));
        
        int text_x = scaledWidth * 12 / 10;
        int y_line0 = height * 5 / 100;
        
        // Split into two lines...
        int lenAvail = width - text_x;
        
        wxArrayString array = splitLine(nameString, dc, lenAvail);
        
        dc.DrawText(array.Item(0), text_x, y_line0);
        int hTitle = dc.GetCharHeight();
        
        if(array.Item(1).Len()){
            dc.DrawText(array.Item(1), text_x + dc.GetCharHeight(), y_line0 + dc.GetCharHeight());
            hTitle += dc.GetCharHeight();
        }
        
        
        int y_line = y_line0 + hTitle;
        dc.DrawLine( text_x, y_line, width - base_offset, y_line);
        
        
        dc.SetFont( *dFont );           // Restore default font
        //int offset = GetCharHeight();
        
        int yPitch = GetCharHeight();
        int yPos = y_line + 4;
        wxString tx;
        
        int text_x_val = scaledWidth + ((width - scaledWidth) * 4 / 10);
        
        // Create and populate the current chart information
//         tx = _("Chart Edition:");
//         dc.DrawText( tx, text_x, yPos);
//         tx = m_pChart->currentChartEdition;
//         dc.DrawText( tx, text_x_val, yPos);
        yPos += yPitch;
        
/*        tx = _("Order Reference:");
        dc.DrawText( tx, text_x, yPos);
        tx = m_pChart->orderRef;
        dc.DrawText( tx, text_x_val, yPos);
 */       yPos += yPitch;
        
//         tx = _("Purchase date:");
//         dc.DrawText( tx, text_x, yPos);
//         tx = m_pChart->purchaseDate;
//         dc.DrawText( tx, text_x_val, yPos);
        yPos += yPitch;
        
//         tx = _("Expiration date:");
//         dc.DrawText( tx, text_x, yPos);
//         tx = m_pChart->expDate;
//         dc.DrawText( tx, text_x_val, yPos);
        yPos += yPitch;
        
        tx = _("Status:");
        dc.DrawText( tx, text_x, yPos);
        tx = m_pChart->getStatusString();
        if(g_statusOverride.Len())
            tx = g_statusOverride;
        dc.DrawText( tx, text_x_val, yPos);
        yPos += yPitch;

    }
    else{
        dc.SetBrush( wxBrush( m_boxColour ) );
    
        GetGlobalColor( _T ( "UITX1" ), &c );
        dc.SetPen( wxPen( c, 1 ) );
    
        int offset = height / 10;
        dc.DrawRectangle( offset, offset, width - (2 * offset), height - (2 * offset));
    
        // Draw the thumbnail
        int scaledHeight = (height - (2 * offset)) * 95 / 100;
        wxBitmap &bm = m_pChart->GetChartThumbnail( scaledHeight );
        
        if(bm.IsOk()){
            dc.DrawBitmap(bm, offset + 3, offset + 3);
        }
        
        int scaledWidth = bm.GetWidth() * scaledHeight / bm.GetHeight();
        
        
        wxFont *dFont = GetOCPNScaledFont_PlugIn(_("Dialog"));
        double font_size = dFont->GetPointSize() * 5/4;
        wxFont *qFont = wxTheFontList->FindOrCreateFont( font_size, dFont->GetFamily(), dFont->GetStyle(), dFont->GetWeight());

        dc.SetFont( *qFont );
        dc.SetTextForeground(wxColour(28, 28, 28));

        int text_x = scaledWidth * 15 / 10;
        int text_y_name = height * 25 / 100;
        
        // Split into two lines...
        int lenAvail = width - text_x;
        
        wxArrayString array = splitLine(nameString, dc, lenAvail);
        
        dc.DrawText(array.Item(0), text_x, text_y_name);
        int hTitle = dc.GetCharHeight();
        
        if(array.Item(1).Len()){
            dc.DrawText(array.Item(1), text_x + dc.GetCharHeight(), text_y_name + dc.GetCharHeight());
            hTitle += dc.GetCharHeight();
        }
        
        
//         if(m_pContainer->GetSelectedChart())
//             dc.SetTextForeground(wxColour(220,220,220));
        
        
        dc.SetFont( *dFont );
        
        wxString tx = _("Status: ") + m_pChart->getStatusString();
        dc.DrawText( tx, text_x + (4 * GetCharHeight()), text_y_name + hTitle);
        
        
    }
    
    
}


BEGIN_EVENT_TABLE( chartScroller, wxScrolledWindow )
//EVT_PAINT(chartScroller::OnPaint)
EVT_ERASE_BACKGROUND(chartScroller::OnEraseBackground)
END_EVENT_TABLE()

chartScroller::chartScroller(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
: wxScrolledWindow(parent, id, pos, size, style)
{
}

void chartScroller::OnEraseBackground(wxEraseEvent& event)
{
    wxASSERT_MSG
    (
        GetBackgroundStyle() == wxBG_STYLE_ERASE,
     "shouldn't be called unless background style is \"erase\""
    );
    
    wxDC& dc = *event.GetDC();
    dc.SetPen(*wxGREEN_PEN);
    
    // clear any junk currently displayed
    dc.Clear();
    
    PrepareDC( dc );
    
    const wxSize size = GetVirtualSize();
    for ( int x = 0; x < size.x; x += 15 )
    {
        dc.DrawLine(x, 0, x, size.y);
    }
    
    for ( int y = 0; y < size.y; y += 15 )
    {
        dc.DrawLine(0, y, size.x, y);
    }
    
    dc.SetTextForeground(*wxRED);
    dc.SetBackgroundMode(wxSOLID);
    dc.DrawText("This text is drawn from OnEraseBackground", 60, 160);
    
}

void chartScroller::DoPaint(wxDC& dc)
{
    PrepareDC(dc);
    
//    if ( m_eraseBgInPaint )
    {
        dc.SetBackground(*wxRED_BRUSH);
        
        // Erase the entire virtual area, not just the client area.
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(GetBackgroundColour());
        dc.DrawRectangle(GetVirtualSize());
        
        dc.DrawText("Background erased in OnPaint", 65, 110);
    }
//     else if ( GetBackgroundStyle() == wxBG_STYLE_PAINT )
//     {
//         dc.SetTextForeground(*wxRED);
//         dc.DrawText("You must enable erasing background in OnPaint to avoid "
//         "display corruption", 65, 110);
//     }
//     
//     dc.DrawBitmap( m_bitmap, 20, 20, true );
//     
//     dc.SetTextForeground(*wxRED);
//     dc.DrawText("This text is drawn from OnPaint", 65, 65);
}

void chartScroller::OnPaint( wxPaintEvent &WXUNUSED(event) )
{
//     if ( m_useBuffer )
//     {
//         wxAutoBufferedPaintDC dc(this);
//         DoPaint(dc);
//     }
//     else
    {
        wxPaintDC dc(this);
        DoPaint(dc);
    }
}


BEGIN_EVENT_TABLE( shopPanel, wxPanel )
EVT_BUTTON( ID_CMD_BUTTON_INSTALL, shopPanel::OnButtonInstall )
EVT_BUTTON( ID_CMD_BUTTON_INSTALL_CHAIN, shopPanel::OnButtonInstallChain )
EVT_BUTTON( ID_CMD_BUTTON_INDEX_CHAIN, shopPanel::OnButtonIndexChain )
EVT_BUTTON( ID_CMD_BUTTON_DOWNLOADLIST_CHAIN, shopPanel::OnDownloadListChain )
EVT_BUTTON( ID_CMD_BUTTON_DOWNLOADLIST_PROC, shopPanel::OnDownloadListProc )
END_EVENT_TABLE()











shopPanel::shopPanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
: wxPanel(parent, id, pos, size, style)
{
    loadShopConfig();
    
    g_CurlEventHandler = new OESENC_CURL_EvtHandler;
    
    g_shopPanel = this;
    m_bcompleteChain = false;
    m_bAbortingDownload = false;
    
    m_ChartSelected = NULL;
    m_choiceSystemName = NULL;
    int ref_len = GetCharHeight();
    
    wxBoxSizer* boxSizerTop = new wxBoxSizer(wxVERTICAL);
    this->SetSizer(boxSizerTop);
    
    wxStaticBoxSizer* staticBoxSizerChartList = new wxStaticBoxSizer( new wxStaticBox(this, wxID_ANY, _("My Charts")), wxVERTICAL);
    boxSizerTop->Add(staticBoxSizerChartList, 0, wxALL|wxEXPAND, WXC_FROM_DIP(5));

    m_buttonUpdate = new wxButton(this, wxID_ANY, _("Refresh Chart List"), wxDefaultPosition, wxDLG_UNIT(this, wxSize(-1,-1)), 0);
    m_buttonUpdate->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(shopPanel::OnButtonUpdate), NULL, this);
    staticBoxSizerChartList->Add(m_buttonUpdate, 1, wxBOTTOM | wxRIGHT | wxALIGN_RIGHT, WXC_FROM_DIP(5));
    
    wxPanel *cPanel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDLG_UNIT(this, wxSize(-1,-1)), wxBG_STYLE_ERASE );
    staticBoxSizerChartList->Add(cPanel, 0, wxALL|wxEXPAND, WXC_FROM_DIP(5));
    wxBoxSizer *boxSizercPanel = new wxBoxSizer(wxVERTICAL);
    cPanel->SetSizer(boxSizercPanel);
    
    m_scrollWinChartList = new wxScrolledWindow(cPanel, wxID_ANY, wxDefaultPosition, wxDLG_UNIT(this, wxSize(-1,-1)), wxBORDER_RAISED | wxVSCROLL | wxBG_STYLE_ERASE );
    m_scrollWinChartList->SetScrollRate(5, 5);
    boxSizercPanel->Add(m_scrollWinChartList, 0, wxALL|wxEXPAND, WXC_FROM_DIP(5));
    
    boxSizerCharts = new wxBoxSizer(wxVERTICAL);
    m_scrollWinChartList->SetSizer(boxSizerCharts);
 
    m_scrollWinChartList->SetMinSize(wxSize(-1,15 * GetCharHeight()));
    staticBoxSizerChartList->SetMinSize(wxSize(-1,16 * GetCharHeight()));
    
    wxString actionString = _("Actions");
    actionString += _T(" [ Version: ") + g_versionString + _T(" ]");
    
    wxStaticBoxSizer* staticBoxSizerAction = new wxStaticBoxSizer( new wxStaticBox(this, wxID_ANY, actionString), wxVERTICAL);
    boxSizerTop->Add(staticBoxSizerAction, 0, wxALL|wxEXPAND, WXC_FROM_DIP(5));

    m_staticLine121 = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDLG_UNIT(this, wxSize(-1,-1)), wxLI_HORIZONTAL);
    staticBoxSizerAction->Add(m_staticLine121, 0, wxALL|wxEXPAND, WXC_FROM_DIP(5));
    
    ///Buttons
    wxGridSizer* gridSizerActionButtons = new wxGridSizer(1, 2, 0, 0);
    staticBoxSizerAction->Add(gridSizerActionButtons, 1, wxALL|wxEXPAND, WXC_FROM_DIP(2));
    
    m_buttonInstall = new wxButton(this, ID_CMD_BUTTON_INSTALL, _("Install Selected Chart"), wxDefaultPosition, wxDLG_UNIT(this, wxSize(-1,-1)), 0);
    gridSizerActionButtons->Add(m_buttonInstall, 1, wxTOP | wxBOTTOM, WXC_FROM_DIP(2));
    
    m_buttonCancelOp = new wxButton(this, wxID_ANY, _("Cancel Operation"), wxDefaultPosition, wxDLG_UNIT(this, wxSize(-1,-1)), 0);
    m_buttonCancelOp->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(shopPanel::OnButtonCancelOp), NULL, this);
    gridSizerActionButtons->Add(m_buttonCancelOp, 1, wxTOP | wxBOTTOM, WXC_FROM_DIP(2));

    wxStaticLine* sLine1 = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDLG_UNIT(this, wxSize(-1,-1)), wxLI_HORIZONTAL);
    staticBoxSizerAction->Add(sLine1, 0, wxALL|wxEXPAND, WXC_FROM_DIP(5));
    
    
    ///Status
    m_staticTextStatus = new wxStaticText(this, wxID_ANY, _("Status: Chart List Refresh required."), wxDefaultPosition, wxDLG_UNIT(this, wxSize(-1,-1)), 0);
    staticBoxSizerAction->Add(m_staticTextStatus, 0, wxALL|wxALIGN_LEFT, WXC_FROM_DIP(5));

    m_ipGauge = new InProgressIndicator(this, wxID_ANY, 100, wxDefaultPosition, wxSize(ref_len * 12, ref_len));
    staticBoxSizerAction->Add(m_ipGauge, 0, wxALL|wxALIGN_CENTER_HORIZONTAL, WXC_FROM_DIP(5));

//     wxString sn = _("System Name:");
//     sn += _T(" ");
//     sn += g_systemName;
//     
//     m_staticTextSystemName = new wxStaticText(this, wxID_ANY, sn, wxDefaultPosition, wxDLG_UNIT(this, wxSize(-1,-1)), 0);
//     staticBoxSizerAction->Add(m_staticTextSystemName, 0, wxALL|wxEXPAND, WXC_FROM_DIP(5));
    
    
    SetName(wxT("shopPanel"));
    //SetSize(500,600);
    if (GetSizer()) {
        GetSizer()->Fit(this);
    }
    
    //  Turn off all buttons initially.
    m_buttonInstall->Hide();
    m_buttonCancelOp->Hide();
    
    UpdateChartList();
    
}

shopPanel::~shopPanel()
{
}

void shopPanel::SelectChart( oeSencChartPanel *chart )
{
    if (m_ChartSelected == chart)
        return;
    
    if (m_ChartSelected)
        m_ChartSelected->SetSelected(false);
    
    m_ChartSelected = chart;
    if (m_ChartSelected)
        m_ChartSelected->SetSelected(true);
    
    m_scrollWinChartList->GetSizer()->Layout();
    
    MakeChartVisible(m_ChartSelected);
    
    UpdateActionControls();
    
    Layout();
    
    Refresh( true );
}

void shopPanel::SelectChartByID( wxString& sku, int index)
{
    for(unsigned int i = 0 ; i < m_panelArray.GetCount() ; i++){
        itemChart *chart = m_panelArray.Item(i)->m_pChart;
        if(sku.IsSameAs(chart->productSKU) && (index == chart->indexSKU)){
            SelectChart(m_panelArray.Item(i));
            MakeChartVisible(m_ChartSelected);
        }
    }
}

void shopPanel::MakeChartVisible(oeSencChartPanel *chart)
{
    if(!chart)
        return;
    
    itemChart *vchart = chart->m_pChart;
    
    for(unsigned int i = 0 ; i < m_panelArray.GetCount() ; i++){
        itemChart *lchart = m_panelArray.Item(i)->m_pChart;
        
        if(vchart->isMatch(lchart)){
                
            int offset = i * chart->GetUnselectedHeight();
            
            m_scrollWinChartList->Scroll(-1, offset / 5);
        }
    }
    

}


void shopPanel::OnButtonUpdate( wxCommandEvent& event )
{
    if(!doLogin())
        return;
    
    setStatusText( _("Contacting Fugawi Charts server..."));
    m_ipGauge->Start();
    wxYield();

    ::wxBeginBusyCursor();
    int err_code = getChartList();
    ::wxEndBusyCursor();
    
    if(err_code != 0){                  // Some error
        wxString ec;
        ec.Printf(_T(" { %d }"), err_code);
        setStatusText( _("Status: Communications error.") + ec);
        m_ipGauge->Stop();
        wxYield();
        return;
    }
    g_chartListUpdatedOK = true;

    setStatusText( _("Status: Ready"));
    m_ipGauge->Stop();
    
    UpdateChartList();
    
    saveShopConfig();
}

void shopPanel::getDownloadList(itemChart *chart)
{
    if(!chart)
        return;
    
    chart->urlArray.Clear();
    
    // Decode the productBody from MIME64 block in GetAccount.XML response
    
    if(!chart->productBody.Len()){
        setStatusText( _("Status: Error reading GetAccount results."));
        m_buttonCancelOp->Hide();
        GetButtonUpdate()->Enable();
        
        g_statusOverride.Clear();
        UpdateChartList();
        
        return;
    }
    
    int flen;
    unsigned char *decodedBody = unbase64( chart->productBody.mb_str(),  chart->productBody.Len(), &flen );
    
    // Parse the xml
    
    TiXmlDocument * doc = new TiXmlDocument();
    doc->Parse( (const char *)decodedBody );
    
    
    TiXmlElement * root = doc->RootElement();
    if(!root)
    {
        setStatusText( _("Status: Error parsing GetAccount results."));
        m_buttonCancelOp->Hide();
        GetButtonUpdate()->Enable();
        
        g_statusOverride.Clear();
        UpdateChartList();
        
        return;                              // undetermined error??
    }
    
    wxString code;
    wxString compilation_date;
    wxString name;
    wxString basedir;
    wxString indexFileURL;
    
    wxString rootName = wxString::FromUTF8( root->Value() );
    TiXmlNode *child;
    for ( child = root->FirstChild(); child != 0; child = child->NextSibling()){
        
        if(!strcmp(child->Value(), "maplib")){
            TiXmlElement *product = child->ToElement();
            code = wxString( product->Attribute( "code" ), wxConvUTF8 );
            compilation_date = wxString( product->Attribute( "compilation_date" ), wxConvUTF8 );
            name = wxString( product->Attribute( "name" ), wxConvUTF8 );
            basedir = wxString( product->Attribute( "basedir" ), wxConvUTF8 );
            indexFileURL = wxString( product->Attribute( "index" ), wxConvUTF8 );
        }
    }

    if(!indexFileURL.Len())
        return;

    if(!basedir.Len())
        return;
    
    if(!indexFileURL.Len())
        code;
    
    // Sometimes the baseDir has trailing "/", sometimes not...
    //  Let us be sure that it does.    
    if(basedir.Last() != '/')    
        basedir += _T("/");
    
    chart->indexBaseDir = basedir;
    chart->shortSetName = code;
    
    // Fetch the chart index XML file
    setStatusText( _("Status: Downloading index file..."));
    m_buttonCancelOp->Show();
    GetButtonUpdate()->Disable();
    
    g_statusOverride = _("Downloading...");
    UpdateChartList();
    
    wxYield();
    
    m_bcompleteChain = true;
    m_bAbortingDownload = false;
    
    //  Create a destination file name for the download.
    wxURI uri;
    uri.Create(indexFileURL);
    
    wxString serverFilename = uri.GetPath();
    wxFileName fn(serverFilename);
    
    wxString downloadIndexDir = g_PrivateDataDir + _T("tmp") + wxFileName::GetPathSeparator();
    if(!::wxDirExists(downloadIndexDir)){
        ::wxMkdir(downloadIndexDir);
    }
        
    wxString downloadIndexFile = downloadIndexDir + fn.GetFullName();

    chart->downloadingFile = downloadIndexFile;
    
    downloadOutStream = new wxFFileOutputStream(downloadIndexFile);
//     if(downloadOutStream->IsOk()){
//         int dd = 4;
//     }
    
    g_downloadChainIdentifier = ID_CMD_BUTTON_INDEX_CHAIN;
    g_curlDownloadThread = new wxCurlDownloadThread(g_CurlEventHandler);
    g_curlDownloadThread->SetURL(indexFileURL);
    g_curlDownloadThread->SetOutputStream(downloadOutStream);
    g_curlDownloadThread->Download();
    
    
}

void shopPanel::OnButtonIndexChain( wxCommandEvent& event )
{
    itemChart *chart = m_ChartSelected->m_pChart;
    if(!chart)
        return;
    
    // Chained through from download end event
    if(m_bcompleteChain){
            
            
            m_bcompleteChain = false;
            
            if(m_bAbortingDownload){
                m_bAbortingDownload = false;
                OCPNMessageBox_PlugIn(NULL, _("Download cancelled."), _("ofc_pi Message"), wxOK);
                m_buttonInstall->Enable();
                return;
            }
            
            //  Download is apparently done.
            g_statusOverride.Clear();
            
            // Read the index file into memory
            
            unsigned char *readBuffer = NULL;
            wxFile indexFile(chart->downloadingFile.mb_str());
            if(indexFile.IsOpened()){
                int unsigned flen = indexFile.Length();
                if(( flen > 0 )  && (flen < 1e7 ) ){                      // Place 10 Mb upper bound on index size 
                    readBuffer = (unsigned char *)malloc( 2 * flen);     // be conservative
                    
                    size_t nRead = indexFile.Read(readBuffer, flen);
                    if(nRead == flen){
                        indexFile.Close();
                        
                        // Good Read, so parse the XML and populate the Array in the chrt item
                        TiXmlDocument * doc = new TiXmlDocument();
                        doc->Parse( (const char *)readBuffer );
                        
                        TiXmlElement * root = doc->RootElement();
                        if(!root){
                            setStatusText( _("Status: Error parsing index file..."));
                            m_buttonCancelOp->Hide();
                            GetButtonUpdate()->Enable();
                            
                            g_statusOverride.Clear();
                            UpdateChartList();
                            
                            return;                        
                        }
                        
                        TiXmlNode *child;
                        for ( child = root->FirstChild(); child != 0; child = child->NextSibling()){
                            const char * t = child->Value();
                            
                            if(!strcmp(child->Value(), "chart")){
                                TiXmlNode *chartx;
                                for ( chartx = child->FirstChild(); chartx != 0; chartx = chartx->NextSibling()){
                                    const char * s = chartx->Value();
                                    if(!strcmp(s, "x_fugawi_bzb_name")){
                                        TiXmlNode *bzb = chartx->FirstChild();
                                        wxString bzbFileName =  wxString::FromUTF8(bzb->Value());
                                        wxString fullBZBUrl = chart->indexBaseDir + bzbFileName;
                                        chart->urlArray.Add( fullBZBUrl );
                                    }
                                }
                            }
                        }
                    }
                }
            }
            free(readBuffer);

            // save a reference to the index file, will be relocated and cleared later
            chart->indexFileTmp = chart->downloadingFile;
            
    }
    
    //  OK, the URL Array is populated
    // Now start the download chain
    chart->indexFileArrayIndex = 0;
    chart->installLocation.Clear();  // Mark as not installed
    
    wxCommandEvent event_next(wxEVT_COMMAND_BUTTON_CLICKED);
    event_next.SetId( ID_CMD_BUTTON_DOWNLOADLIST_PROC );
    GetEventHandler()->AddPendingEvent(event_next);
    
    
    
    return;
}


void shopPanel::OnDownloadListProc( wxCommandEvent& event )
{
    itemChart *chart = m_ChartSelected->m_pChart;
    if(!chart)
        return;
    
    // Download the BZB files listed in the array
    
    m_bcompleteChain = true;
    m_bAbortingDownload = false;
    
    // the target url
    if(chart->indexFileArrayIndex >= chart->urlArray.GetCount() ){               // some counting error
        setStatusText( _("Status: Error parsing index.xml."));
        m_buttonCancelOp->Hide();
        GetButtonUpdate()->Enable();
        g_statusOverride.Clear();
        UpdateChartList();
        return;                              // undetermined error??
    }
    
    // Advance to the next required download
    while(chart->indexFileArrayIndex < chart->urlArray.GetCount()){               
        wxString tUrl = chart->urlArray.Item(chart->indexFileArrayIndex);
        wxURI uri;
        uri.Create(tUrl);
        wxString serverFilename = uri.GetPath();
        wxFileName fn(serverFilename);
        
        // Is the next BAP file in place already, and current?
        wxString BAPfile = chart->installLocationTentative + wxFileName::GetPathSeparator();
        BAPfile += chart->shortSetName + wxFileName::GetPathSeparator() + fn.GetName() + _T(".BAP");
        if(::wxFileExists( BAPfile )){
            chart->indexFileArrayIndex++;               // Skip to the next target
        }
        else{
            break;   // the while
        }
    }
    
    
    return chainToNextChart(chart);

}



void shopPanel::OnDownloadListChain( wxCommandEvent& event )
{
    itemChart *chart = m_ChartSelected->m_pChart;
    if(!chart)
        return;

    // Chained through from download end event
    if(m_bcompleteChain){
            
        wxFileName fn(chart->downloadingFile);
        wxString installDir = chart->installLocationTentative + wxFileName::GetPathSeparator() + chart->shortSetName;
        
        m_bcompleteChain = false;
            
        if(m_bAbortingDownload){
            m_bAbortingDownload = false;
            OCPNMessageBox_PlugIn(NULL, _("Chart download cancelled."), _("ofc_pi Message"), wxOK);
            m_buttonInstall->Enable();
            
            g_statusOverride.Clear();
            UpdateChartList();
            
            return;
        }
        
        bool bSuccess = true;
        if(!wxFileExists( chart->downloadingFile )){
            bSuccess = false;
        }
        
        // BZB File is ready
        
        if(bSuccess){
            if(!validate_server()){
                setStatusText( _("Status: Server unavailable."));
                m_buttonCancelOp->Hide();
                GetButtonUpdate()->Enable();
                g_statusOverride.Clear();
                UpdateChartList();
                return;
            }
            
        }
        
        if(bSuccess){
            // location for decrypted BZB file
            
            fn.SetExt(_T("zip"));
            
            xtr1_inStream decoder;
            bool result = decoder.decryptBZB(chart->downloadingFile, fn.GetFullPath());
            
            if(!result){
                bSuccess = false;
            }
        }
            
        if(bSuccess){
            // Unzip and extract the .BAP file to target location
            bool zipret = ExtractZipFiles( fn.GetFullPath(), installDir, false, wxDateTime::Now(), false);
            
            if(!zipret){
                bSuccess = false;
            }
        }
         
//         if(chart->indexFileArrayIndex == 5)
//             bSuccess = false;
 
        // clean up
        ::wxRemoveFile(fn.GetFullPath());               // the decrypted zip file
        ::wxRemoveFile(chart->downloadingFile);         // the downloaded BZB file
 
            // Success for this file...
        if(bSuccess) {   
            chart->indexFileArrayIndex++;               // move to the next file

            while(chart->indexFileArrayIndex < chart->urlArray.GetCount()){               
                wxString tUrl = chart->urlArray.Item(chart->indexFileArrayIndex);
                wxURI uri;
                uri.Create(tUrl);
                wxString serverFilename = uri.GetPath();
                wxFileName fn(serverFilename);
            
            // Is the next BAP file in place already, and current?
                wxString BAPfile = chart->installLocationTentative + wxFileName::GetPathSeparator();
                BAPfile += chart->shortSetName + wxFileName::GetPathSeparator() + fn.GetName() + _T(".BAP");
                if(::wxFileExists( BAPfile )){
                    chart->indexFileArrayIndex++;               // Skip to the next target
                }
                else{
                    break;   // the while
                }
            }
            
            return chainToNextChart(chart);
        }       // bSuccess
        else {                  // on no success with this file, we might try again
            dlTryCount++;
            if(dlTryCount > N_RETRY){
                setStatusText( _("Status: BZB download FAILED after retry.") + _T("  [") + chart->downloadingFile + _T("]") );
                m_buttonCancelOp->Hide();
                GetButtonUpdate()->Enable();
                g_statusOverride.Clear();
                UpdateChartList();
                return;
            }
            else{
                chainToNextChart(chart, dlTryCount);            // Retry the same chart
            }
        }        
            
        
    }
}


void shopPanel::chainToNextChart(itemChart *chart, int ntry)
{
    bool bContinue = chart->indexFileArrayIndex < chart->urlArray.GetCount();
    
    //bool bContinue = chart->indexFileArrayIndex < 10;        /// testing
    
    if(!bContinue){
        // Record the full install location
        chart->installLocation = chart->installLocationTentative;
        wxString installDir = chart->installLocation + wxFileName::GetPathSeparator() + chart->shortSetName;
        chart->chartInstallLocnFull = installDir;
 
        
        //  Create the chartInfo file
        wxString infoFile = installDir + wxFileName::GetPathSeparator() + _T("chartInfo.txt");
        if(wxFileExists( infoFile ))
            ::wxRemoveFile(infoFile);
        
        wxTextFile info;
        info.Create(infoFile);
        info.AddLine(_T("productSKU=") + chart->productSKU);
        info.AddLine(_T("productKey=") + chart->productKey);
        info.Write();
        info.Close();
        
        // Save a copy of the index file
        if(chart->indexFileTmp.Len()){
            wxString indexFile = installDir + wxFileName::GetPathSeparator() + _T("index.xml");
            ::wxCopyFile(chart->indexFileTmp, indexFile);
        }
        ::wxRemoveFile(chart->indexFileTmp);
        
        //  Add the target install directory to core dir list if necessary
        //  If the currect core chart directories do not cover this new directory, then add it
        bool covered = false;
        for( size_t i = 0; i < GetChartDBDirArrayString().GetCount(); i++ )
        {
            if( installDir.StartsWith((GetChartDBDirArrayString().Item(i))) )
            {
                covered = true;
                break;
            }
        }
        if( !covered )
        {
            AddChartDirectory( installDir );
        }
        
        // Clean up the UI
        g_dlStatPrefix.Clear();
        setStatusText( _("Status: Ready"));
        
        OCPNMessageBox_PlugIn(NULL, _("Chart installation complete."), _("ofc_pi Message"), wxOK);
        
        GetButtonUpdate()->Enable();
        
        g_statusOverride.Clear();
        UpdateChartList();
        
        saveShopConfig();
        
        return;
    }
    else{                           // not done yet, carry on with the list
        m_bcompleteChain = true;
        m_bAbortingDownload = false;
                
                // the target url
        wxString tUrl = chart->urlArray.Item(chart->indexFileArrayIndex);
                
                //  Create a destination file name for the download.
        wxURI uri;
        uri.Create(tUrl);
                
        wxString serverFilename = uri.GetPath();
        wxFileName fn(serverFilename);
        
        wxString downloadTmpDir = g_PrivateDataDir + _T("tmp") + wxFileName::GetPathSeparator();
        if(!::wxDirExists(downloadTmpDir)){
              ::wxMkdir(downloadTmpDir);
        }
                
        wxString downloadBZBFile = downloadTmpDir + fn.GetName() + _T(".BZB");
                
        chart->downloadingFile = downloadBZBFile;
        dlTryCount = ntry;
                
        downloadOutStream = new wxFFileOutputStream(downloadBZBFile);
                
        wxString statIncremental =_("Downloading chart:") + _T(" ") + fn.GetName() + _T(" ");
        wxString i1;  i1.Printf(_T("(%d/%d) "), chart->indexFileArrayIndex + 1, chart->urlArray.GetCount());
        g_dlStatPrefix = statIncremental + i1;
                
        m_buttonCancelOp->Show();
                
        g_downloadChainIdentifier = ID_CMD_BUTTON_DOWNLOADLIST_CHAIN;
        g_curlDownloadThread = new wxCurlDownloadThread(g_CurlEventHandler);
        g_curlDownloadThread->SetURL(tUrl);
        g_curlDownloadThread->SetOutputStream(downloadOutStream);
        g_curlDownloadThread->Download();
                
        return;
    }
}





void shopPanel::OnButtonInstallChain( wxCommandEvent& event )
{
    return;
}


void shopPanel::OnButtonInstall( wxCommandEvent& event )
{
    m_buttonInstall->Disable();
    m_buttonCancelOp->Show();
    
    itemChart *chart = m_ChartSelected->m_pChart;
    if(!chart)
        return;
    
   
    // Is chart already in "download" state for me?
        if((chart->getChartStatus() == STAT_READY_DOWNLOAD) || (chart->getChartStatus() == STAT_CURRENT)) {   
        
        // Get the target install directory
        wxString installDir = chart->installLocation;
        wxString chosenInstallDir;
        
        if(1/*!installDir.Length()*/){
            
            wxString installLocn = g_PrivateDataDir;
            if(installDir.Length())
                installLocn = installDir;
            else if(g_lastInstallDir.Length())
                installLocn = g_lastInstallDir;
            
            wxDirDialog dirSelector( NULL, _("Choose chart install location."), installLocn, wxDD_DEFAULT_STYLE  );
            int result = dirSelector.ShowModal();
            
            if(result == wxID_OK){
                chosenInstallDir = dirSelector.GetPath();
                chart->installLocationTentative = chosenInstallDir;
                g_lastInstallDir = chosenInstallDir;
            }
            else{
                setStatusText( _("Status:  Cancelled."));
                m_buttonCancelOp->Hide();
                GetButtonUpdate()->Enable();
                
                g_statusOverride.Clear();
                UpdateChartList();
                
                return;
            }
        }
        
        m_startedDownload = false;
        getDownloadList(chart);
        return;
    }
    
    // Otherwise, do the activate step
    int activateResult;
    activateResult = doActivate(chart);
    if(activateResult != 0){
        setStatusText( _("Status:  Activation FAILED."));
        m_buttonCancelOp->Hide();
        GetButtonUpdate()->Enable();
        
        g_statusOverride.Clear();
        UpdateChartList();
        m_buttonInstall->Enable();
        return;
    }
    
    // Activation appears successful
    
    setStatusText( _("Contacting Fugawi Charts server..."));
    m_ipGauge->Start();
    wxYield();
    
    ::wxBeginBusyCursor();
    int err_code = getChartList();
    ::wxEndBusyCursor();
    
    if(err_code != 0){                  // Some error
        wxString ec;
        ec.Printf(_T(" { %d }"), err_code);
        setStatusText( _("Status: Communications error.") + ec);
        m_ipGauge->Stop();
        wxYield();
        return;
    }
    g_chartListUpdatedOK = true;
    
    setStatusText( _("Status: Ready"));
    m_buttonCancelOp->Hide();
    
    m_ipGauge->Stop();
    
    UpdateChartList();
    
    return;
    
}






int shopPanel::doDownloadGui()
{
    setStatusText( _("Status: Downloading..."));
    //m_staticTextStatusProgress->Show();
    m_buttonCancelOp->Show();
    GetButtonUpdate()->Disable();
    
    g_statusOverride = _("Downloading...");
    UpdateChartList();
    
    wxYield();
    
    m_bcompleteChain = true;
    m_bAbortingDownload = false;
    
    doDownload(m_ChartSelected);
    
    return 0;
}

void shopPanel::OnButtonCancelOp( wxCommandEvent& event )
{
    
    if(g_curlDownloadThread){
        m_bAbortingDownload = true;
        g_curlDownloadThread->Abort();
        m_ipGauge->SetValue(0);
        setStatusTextProgress(_T(""));
        m_bcompleteChain = true;
    }
    
    setStatusText( _("Status: OK"));
    m_buttonCancelOp->Hide();
    
    g_statusOverride.Clear();
    m_buttonInstall->Enable();
    
    UpdateChartList();
    
}



void shopPanel::UpdateChartList( )
{
    // Capture the state of any selected chart
    if(m_ChartSelected){
        itemChart *chart = m_ChartSelected->m_pChart;
        if(chart){
            m_ChartSelectedSKU = chart->productSKU;           // save a copy of the selected chart
            m_ChartSelectedIndex = chart->indexSKU;
        }
    }
    
    m_scrollWinChartList->ClearBackground();
    
    // Clear any existing panels
    for(unsigned int i = 0 ; i < m_panelArray.GetCount() ; i++){
        delete m_panelArray.Item(i);
    }
    m_panelArray.Clear();
    m_ChartSelected = NULL;

    
    // Add new panels
    // Clear all flags
    for(unsigned int i=0 ; i < g_ChartArray.GetCount() ; i++){ g_ChartArray.Item(i)->display_flags= 0; }
    
    // Add the charts relevant to this device
    for(unsigned int i=0 ; i < g_ChartArray.GetCount() ; i++){
        itemChart *c1 = g_ChartArray.Item(i);
        if(!g_chartListUpdatedOK || !c1->isChartsetDontShow()){
            c1->display_flags= 1;
        }
    }
    
    // Remove duplicates by finding them, and selecting the most useful chart for the list
    for(unsigned int i=0 ; i < g_ChartArray.GetCount() ; i++){
        itemChart *c1 = g_ChartArray.Item(i);
        
        if(c1->display_flags ==0)
            continue;
        
        bool bdup = false;
        for(unsigned int j=i+1 ; j < g_ChartArray.GetCount() ; j++){
            itemChart *c2 = g_ChartArray.Item(j);

            if(c2->display_flags ==0)
                continue;
            
            if(c1->productSKU == c2->productSKU){
                // A duplicate.  Choose the best
                if(c1->bActivated && !c2->bActivated){
                    c1->display_flags += 2;     // choose activated one
                    c2->display_flags = 0;
                }
                else if(c2->bActivated && !c1->bActivated){
                    c2->display_flags += 2;     // choose activated one
                    c1->display_flags = 0;
                }
                else if(!c2->bActivated && !c1->bActivated){
                    c1->display_flags += 2;     // choose first one 
                    c2->display_flags = 0;
                }
                
                bdup = true;
            }
        }
        if(!bdup)
            c1->display_flags += 2;            
     }

    // New create the displayable list
    for(unsigned int i=0 ; i < g_ChartArray.GetCount() ; i++){
        itemChart *c1 = g_ChartArray.Item(i);
        if(!g_chartListUpdatedOK || (c1->display_flags > 2) ){
            oeSencChartPanel *chartPanel = new oeSencChartPanel( m_scrollWinChartList, wxID_ANY, wxDefaultPosition, wxSize(-1, -1), g_ChartArray.Item(i), this);
            chartPanel->SetSelected(false);
        
            boxSizerCharts->Add( chartPanel, 0, wxEXPAND|wxALL, 0 );
            m_panelArray.Add( chartPanel );
        } 
    }
    
    SelectChartByID(m_ChartSelectedSKU, m_ChartSelectedIndex);
    
    m_scrollWinChartList->ClearBackground();
    m_scrollWinChartList->GetSizer()->Layout();

    Layout();

    m_scrollWinChartList->ClearBackground();
    
    UpdateActionControls();
    
    //saveShopConfig();
    
    Refresh( true );
}


void shopPanel::UpdateActionControls()
{
    //  Turn off all buttons.
    m_buttonInstall->Hide();
    
    
    if(!m_ChartSelected){                // No chart selected
        m_buttonInstall->Enable();
        return;
    }
    
    if(!g_statusOverride.Length()){
        m_buttonInstall->Enable();
    }
    
    itemChart *chart = m_ChartSelected->m_pChart;


    if(chart->getChartStatus() == STAT_PURCHASED){
        m_buttonInstall->SetLabel(_("Install Selected Chartset"));
        m_buttonInstall->Show();
    }
    else if(chart->getChartStatus() == STAT_CURRENT){
        m_buttonInstall->SetLabel(_("Reinstall Selected Chartset"));
        m_buttonInstall->Show();
    }
    else if(chart->getChartStatus() == STAT_STALE){
        m_buttonInstall->SetLabel(_("Update Selected Chartset"));
        m_buttonInstall->Show();
    }
    else if(chart->getChartStatus() == STAT_READY_DOWNLOAD){
        m_buttonInstall->SetLabel(_("Install Selected Chartset"));
        m_buttonInstall->Show();       
    }
    else if(chart->getChartStatus() == STAT_REQUESTABLE){
        m_buttonInstall->SetLabel(_("Activate Selected Chartset"));
        m_buttonInstall->Show();
    }
    
}



 //------------------------------------------------------------------
 
 
 BEGIN_EVENT_TABLE( InProgressIndicator, wxGauge )
 EVT_TIMER( 4356, InProgressIndicator::OnTimer )
 END_EVENT_TABLE()
 
 InProgressIndicator::InProgressIndicator()
 {
 }
 
 InProgressIndicator::InProgressIndicator(wxWindow* parent, wxWindowID id, int range,
                     const wxPoint& pos, const wxSize& size,
                     long style, const wxValidator& validator, const wxString& name)
{
    wxGauge::Create(parent, id, range, pos, size, style, validator, name);
    
//    m_timer.Connect(wxEVT_TIMER, wxTimerEventHandler( InProgressIndicator::OnTimer ), NULL, this);
    m_timer.SetOwner( this, 4356 );
    m_timer.Start( 50 );
    
    m_bAlive = false;
    
}

InProgressIndicator::~InProgressIndicator()
{
}
 
void InProgressIndicator::OnTimer(wxTimerEvent &evt)
{
    if(m_bAlive)
        Pulse();
}
 
 
void InProgressIndicator::Start() 
{
     m_bAlive = true;
}
 
void InProgressIndicator::Stop() 
{
     m_bAlive = false;
     SetValue(0);
}


//-------------------------------------------------------------------------------------------
OESENC_CURL_EvtHandler::OESENC_CURL_EvtHandler()
{
    Connect(wxCURL_BEGIN_PERFORM_EVENT, (wxObjectEventFunction)(wxEventFunction)&OESENC_CURL_EvtHandler::onBeginEvent);
    Connect(wxCURL_END_PERFORM_EVENT, (wxObjectEventFunction)(wxEventFunction)&OESENC_CURL_EvtHandler::onEndEvent);
    Connect(wxCURL_DOWNLOAD_EVENT, (wxObjectEventFunction)(wxEventFunction)&OESENC_CURL_EvtHandler::onProgressEvent);
    
}

OESENC_CURL_EvtHandler::~OESENC_CURL_EvtHandler()
{
}

void OESENC_CURL_EvtHandler::onBeginEvent(wxCurlBeginPerformEvent &evt)
{
 //   OCPNMessageBox_PlugIn(NULL, _("DLSTART."), _("oeSENC_PI Message"), wxOK);
    g_shopPanel->m_startedDownload = true;
}

void OESENC_CURL_EvtHandler::onEndEvent(wxCurlEndPerformEvent &evt)
{
 //   OCPNMessageBox_PlugIn(NULL, _("DLEnd."), _("oeSENC_PI Message"), wxOK);
    
    g_shopPanel->getInProcessGuage()->SetValue(0);
    g_shopPanel->setStatusTextProgress(_T(""));
    g_shopPanel->setStatusText( _("Status: OK"));
    g_shopPanel->m_buttonCancelOp->Hide();
    //g_shopPanel->GetButtonDownload()->Hide();
    g_shopPanel->GetButtonUpdate()->Enable();
    
    if(downloadOutStream){
        downloadOutStream->Close();
        delete downloadOutStream;
        downloadOutStream = NULL;
    }
    
    g_curlDownloadThread = NULL;

    if(g_shopPanel->m_bAbortingDownload){
        if(g_shopPanel->GetSelectedChart()){
            itemChart *chart = g_shopPanel->GetSelectedChart()->m_pChart;
            if(chart){
                chart->downloadingFile.Clear();
            }
        }
    }
            
    //  Send an event to chain back to "Install" button
    wxCommandEvent event(wxEVT_COMMAND_BUTTON_CLICKED);
    event.SetId( g_downloadChainIdentifier/*ID_CMD_BUTTON_INSTALL_CHAIN*/ );
    g_shopPanel->GetEventHandler()->AddPendingEvent(event);
    
}

double dl_now;
double dl_total;
time_t g_progressTicks;

void OESENC_CURL_EvtHandler::onProgressEvent(wxCurlDownloadEvent &evt)
{
    dl_now = evt.GetDownloadedBytes();
    dl_total = evt.GetTotalBytes();
    
    // Calculate the gauge value
    if(evt.GetTotalBytes() > 0){
        float progress = evt.GetDownloadedBytes()/evt.GetTotalBytes();
        g_shopPanel->getInProcessGuage()->SetValue(progress * 100);
    }
    
    wxDateTime now = wxDateTime::Now();
    if(now.GetTicks() != g_progressTicks){
        std::string speedString = evt.GetHumanReadableSpeed(" ", 0);
    
    //  Set text status
        wxString tProg;
        tProg = _("Downloaded:  ");
        wxString msg;
        msg.Printf( _T("(%6.1f MiB / %4.0f MiB)    "), (float)(evt.GetDownloadedBytes() / 1e6), (float)(evt.GetTotalBytes() / 1e6));
        msg += wxString( speedString.c_str(), wxConvUTF8);
        tProg += msg;
        
        if(g_dlStatPrefix.Len())
            tProg = g_dlStatPrefix + msg;
            
        g_shopPanel->setStatusTextProgress( tProg );
        
        g_progressTicks = now.GetTicks();
    }
    
}

IMPLEMENT_DYNAMIC_CLASS( xtr1Login, wxDialog )
BEGIN_EVENT_TABLE( xtr1Login, wxDialog )
EVT_BUTTON( ID_GETIP_CANCEL, xtr1Login::OnCancelClick )
EVT_BUTTON( ID_GETIP_OK, xtr1Login::OnOkClick )
END_EVENT_TABLE()


xtr1Login::xtr1Login()
{
}

xtr1Login::xtr1Login( wxWindow* parent, wxWindowID id, const wxString& caption,
                                          const wxPoint& pos, const wxSize& size, long style )
{
    
    long wstyle = wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER;
    wxDialog::Create( parent, id, caption, pos, size, wstyle );
    
    wxFont *qFont = GetOCPNScaledFont_PlugIn(_("Dialog"));
    SetFont( *qFont );
    
    CreateControls();
    GetSizer()->SetSizeHints( this );
    Centre();
    
}

xtr1Login::~xtr1Login()
{
}

/*!
 * oeSENCLogin creator
 */

bool xtr1Login::Create( wxWindow* parent, wxWindowID id, const wxString& caption,
                                  const wxPoint& pos, const wxSize& size, long style )
{
    SetExtraStyle( GetExtraStyle() | wxWS_EX_BLOCK_EVENTS );
    
    long wstyle = style;
    #ifdef __WXMAC__
    wstyle |= wxSTAY_ON_TOP;
    #endif
    wxDialog::Create( parent, id, caption, pos, size, wstyle );
    
    wxFont *qFont = GetOCPNScaledFont_PlugIn(_("Dialog"));
    SetFont( *qFont );
    
    
    CreateControls(  );
    Centre();
    return TRUE;
}


void xtr1Login::CreateControls(  )
{
    int ref_len = GetCharHeight();
    
    xtr1Login* itemDialog1 = this;
    
    wxBoxSizer* itemBoxSizer2 = new wxBoxSizer( wxVERTICAL );
    itemDialog1->SetSizer( itemBoxSizer2 );
    
    wxStaticBox* itemStaticBoxSizer4Static = new wxStaticBox( itemDialog1, wxID_ANY, _("Login to Fugawi Charts server") );
    
    wxStaticBoxSizer* itemStaticBoxSizer4 = new wxStaticBoxSizer( itemStaticBoxSizer4Static, wxVERTICAL );
    itemBoxSizer2->Add( itemStaticBoxSizer4, 0, wxEXPAND | wxALL, 5 );
    
    itemStaticBoxSizer4->AddSpacer(10);
    
    wxStaticLine *staticLine121 = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDLG_UNIT(this, wxSize(-1,-1)), wxLI_HORIZONTAL);
    itemStaticBoxSizer4->Add(staticLine121, 0, wxALL|wxEXPAND, WXC_FROM_DIP(5));
    
    wxFlexGridSizer* flexGridSizerActionStatus = new wxFlexGridSizer(0, 2, 0, 0);
    flexGridSizerActionStatus->SetFlexibleDirection( wxBOTH );
    flexGridSizerActionStatus->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_SPECIFIED );
    flexGridSizerActionStatus->AddGrowableCol(0);
    
    itemStaticBoxSizer4->Add(flexGridSizerActionStatus, 1, wxALL|wxEXPAND, WXC_FROM_DIP(5));
    
    wxStaticText* itemStaticText5 = new wxStaticText( itemDialog1, wxID_STATIC, _("email address:"), wxDefaultPosition, wxDefaultSize, 0 );
    flexGridSizerActionStatus->Add( itemStaticText5, 0, wxALIGN_LEFT | wxLEFT | wxRIGHT | wxTOP | wxADJUST_MINSIZE, 5 );
    
    m_UserNameCtl = new wxTextCtrl( itemDialog1, ID_GETIP_IP, _T(""), wxDefaultPosition, wxSize( ref_len * 10, -1 ), 0 );
    flexGridSizerActionStatus->Add( m_UserNameCtl, 0,  wxALIGN_CENTER | wxLEFT | wxRIGHT | wxBOTTOM , 5 );
    
 
    wxStaticText* itemStaticText6 = new wxStaticText( itemDialog1, wxID_STATIC, _("Password:"), wxDefaultPosition, wxDefaultSize, 0 );
    flexGridSizerActionStatus->Add( itemStaticText6, 0, wxALIGN_LEFT | wxLEFT | wxRIGHT | wxTOP | wxADJUST_MINSIZE, 5 );
    
    m_PasswordCtl = new wxTextCtrl( itemDialog1, ID_GETIP_IP, _T(""), wxDefaultPosition, wxSize( ref_len * 10, -1 ), wxTE_PASSWORD );
    flexGridSizerActionStatus->Add( m_PasswordCtl, 0,  wxALIGN_CENTER | wxLEFT | wxRIGHT | wxBOTTOM , 5 );
    
    
    wxBoxSizer* itemBoxSizer16 = new wxBoxSizer( wxHORIZONTAL );
    itemBoxSizer2->Add( itemBoxSizer16, 0, wxALIGN_RIGHT | wxALL, 5 );
    
    m_CancelButton = new wxButton( itemDialog1, ID_GETIP_CANCEL, _("Cancel"), wxDefaultPosition, wxDefaultSize, 0 );
    itemBoxSizer16->Add( m_CancelButton, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5 );
    
    m_OKButton = new wxButton( itemDialog1, ID_GETIP_OK, _("OK"), wxDefaultPosition, wxDefaultSize, 0 );
    m_OKButton->SetDefault();
    
    itemBoxSizer16->Add( m_OKButton, 0, wxALIGN_CENTER_VERTICAL | wxALL, 5 );
    
    
}


bool xtr1Login::ShowToolTips()
{
    return TRUE;
}



void xtr1Login::OnCancelClick( wxCommandEvent& event )
{
    EndModal(2);
}

void xtr1Login::OnOkClick( wxCommandEvent& event )
{
    if( (m_UserNameCtl->GetValue().Length() == 0 ) || (m_PasswordCtl->GetValue().Length() == 0 ) ){
        SetReturnCode(1);
        EndModal(1);
    }
    else {
        SetReturnCode(0);
        EndModal(0);
    }
}

void xtr1Login::OnClose( wxCloseEvent& event )
{
    SetReturnCode(2);
}

