#ifndef DECODING_XML_FILE_HPP_INCLUDE_GUARD_GJH676SFG8976BY
#define DECODING_XML_FILE_HPP_INCLUDE_GUARD_GJH676SFG8976BY

//#include 
//#include 
#import "MSXML3.dll" named_guids

using namespace std;


namespace utility
{
	MSXML2::IXMLDOMDocumentPtr OpenXMLfile(CString xmlfile )
	{
		//open the xml file
		MSXML2::IXMLDOMDocumentPtr pDOMDoc(__uuidof(MSXML2::DOMDocument));	
		pDOMDoc->put_async(VARIANT_FALSE);
		
		_bstr_t bstrURL = LPCTSTR(xmlfile);

		try{
			VARIANT_BOOL varResult = pDOMDoc->load(_variant_t(bstrURL));
				
			if (VARIANT_FALSE == varResult)
				 throw "Fail to load XML file.";
		}
		catch(char *str )
		{
		  //MessageBox(NULL, str, "Error Message", MB_OK);
		  //return "";
		  throw "Fail to load XML file.";
		}

		return pDOMDoc;
	}

	/**
	*	starting from pNode, this func recursively search all the path
	*	if the condition specified by the last four params is met, return the value in CString format
	*	otherwise finally an empty CString will be returned.
	*/
	CString queryAttrValueFromNode(MSXML2::IXMLDOMNodePtr pNode, CString nodename, CString attrname1, CString attrvalue1, CString attrname2)
	{
		CString strText = (LPCTSTR)(pNode->GetnodeName());
		MSXML2::DOMNodeType nodeType;
		pNode->get_nodeType(&nodeType);

		if(nodeType==MSXML2::NODE_TEXT)//actually this is not strict, should return empty
		{
			_bstr_t name = pNode ->GetparentNode()->GetnodeName();
			if(name == _bstr_t(nodename))
			{
				_bstr_t val = _bstr_t(pNode ->GetnodeValue());
				return (LPCSTR)val;
			}
		}
		else if( nodeType==MSXML2::NODE_ELEMENT )
		{
			if(nodename == strText)
			{
				// attributes
				MSXML2::IXMLDOMNamedNodeMapPtr pAttrMap = pNode->Getattributes();
				if (pAttrMap != NULL)
				{
					_bstr_t name, value;

					MSXML2::IXMLDOMAttributePtr pAttribute;
					while(pAttribute = pAttrMap->nextNode())
					{
						name = pAttribute->GetnodeName();
						value = _bstr_t(pAttribute->GetnodeValue());

						if(name==_bstr_t(attrname1) && value == _bstr_t(attrvalue1))
						{
							MSXML2::IXMLDOMNodePtr pValue = pAttrMap -> getNamedItem(LPCTSTR(attrname2));
							CString str =(LPCSTR)pValue ->Gettext();
							return str;
						}
					}
				}
			}
			else
			{
				MSXML2::IXMLDOMNodeListPtr pNodeList = pNode -> GetchildNodes();
				MSXML2::IXMLDOMNodePtr pChild2;
				while(pChild2 = pNodeList->nextNode())
				{ 
					CString str = queryAttrValueFromNode(pChild2, nodename, attrname1, attrvalue1, attrname2);
					if(!str.IsEmpty())
						return str;
				}
			}
		}

		return "";
	}

	/**
	*	starting from pNode, this func recursively search all the path
	*	if the condition specified by the last four params is met, return the value in CString format
	*	otherwise finally an empty CString will be returned.
	*/
	CString queryAttrValueFromNode(MSXML2::IXMLDOMNodePtr pNode, CString nodename, /*CString attrname1, CString attrvalue1,*/ CString attrname)
	{
		CString strText = (LPCTSTR)(pNode->GetnodeName());
		MSXML2::DOMNodeType nodeType;
		pNode->get_nodeType(&nodeType);

		if(nodeType==MSXML2::NODE_TEXT)//actually this is not strict, should return empty
		{
			_bstr_t name = pNode ->GetparentNode()->GetnodeName();
			if(name == _bstr_t(nodename))
			{
				_bstr_t val = _bstr_t(pNode ->GetnodeValue());
				return (LPCSTR)val;
			}
		}
		else if( nodeType==MSXML2::NODE_ELEMENT )
		{
			if(nodename == strText)
			{
				// attributes
				MSXML2::IXMLDOMNamedNodeMapPtr pAttrMap = pNode->Getattributes();
				if (pAttrMap != NULL)
				{
					_bstr_t name, value;

					MSXML2::IXMLDOMAttributePtr pAttribute;
					while(pAttribute = pAttrMap->nextNode())
					{
						name = pAttribute->GetnodeName();
						if(name==_bstr_t(attrname))
						{
							value = _bstr_t(pAttribute->GetnodeValue());
							CString str =(LPCSTR)value;
							return str;
						}
					}
				}
			}
			else
			{
				MSXML2::IXMLDOMNodeListPtr pNodeList = pNode -> GetchildNodes();
				MSXML2::IXMLDOMNodePtr pChild2;
				while(pChild2 = pNodeList->nextNode())
				{ 
					CString str = queryAttrValueFromNode(pChild2, nodename, attrname);
					if(!str.IsEmpty())
						return str;
				}
			}
		}

		return "";
	}

	/**
	*	starting from pNode, this func recursively search all the path
	*	if the condition specified by the last four params is met, return the value in CString format
	*	otherwise finally an empty CString will be returned.
	*/
	vector< pair<CString, CString> > queryAttrsByNodeName(MSXML2::IXMLDOMNodePtr pNode, CString nodename)
	{
		vector< pair<CString, CString> > vec;

		CString strText = (LPCTSTR)(pNode->GetnodeName());
		MSXML2::DOMNodeType nodeType;
		pNode->get_nodeType(&nodeType);

		if( nodeType==MSXML2::NODE_ELEMENT )
		{
			if(nodename == strText)
			{
				// attributes
				MSXML2::IXMLDOMNamedNodeMapPtr pAttrMap = pNode->Getattributes();
				if (pAttrMap != NULL)
				{
					_bstr_t name, value;

					MSXML2::IXMLDOMAttributePtr pAttribute;
					while(pAttribute = pAttrMap->nextNode())
					{
						name = pAttribute->GetnodeName();
						value = _bstr_t(pAttribute->GetnodeValue());

						std::pair<CString, CString> p((LPCSTR)name, (LPCSTR)value);
						vec.push_back(p);		
					}
				}
			}
			else
			{
				MSXML2::IXMLDOMNodeListPtr pNodeList = pNode -> GetchildNodes();
				MSXML2::IXMLDOMNodePtr pChild2;
				while(pChild2 = pNodeList->nextNode())
				{ 
					vector< pair<CString, CString> > v = queryAttrsByNodeName(pChild2, nodename);
					if(!v.empty())
						return v;
				}
			}
		}

		return vec;
	}

	/**
	* get the value of attribute attrname2 from node nodename where attribute attrname1=attrvalue1
	*
	* !It's targeted at Softscan .vol file, it's actually an xml file
	*/
	CString getAttrValue(MSXML2::IXMLDOMDocumentPtr pDOMDoc, CString nodename, CString attrname1, CString attrvalue1, CString attrname2)
	{

		MSXML2::IXMLDOMNodeListPtr pChildNodes = pDOMDoc->GetchildNodes();
		if (pChildNodes == NULL)//seems this never gonna happen, no harm though
			return "";

		USES_CONVERSION;

		MSXML2::IXMLDOMNodePtr pChild;
		try
		{
			while(pChild = pChildNodes->nextNode())
			{
				CString str = queryAttrValueFromNode(pChild, nodename, attrname1, attrvalue1, attrname2);
				if(!str.IsEmpty())
					return str;
			
			}
		}
		catch(_com_error e)
		{
			throw "Fail to decode XML file.";
		}
		catch(...)
		{
			throw "Fail to decode XML file.";
		}	

		return "";
	}

	/**
	* get the value of attribute attrname2 from node nodename where attribute attrname1=attrvalue1
	*
	* !It's targeted at Softscan .vol file, it's actually an xml file
	*/
	CString getAttrValue(MSXML2::IXMLDOMDocumentPtr pDOMDoc, CString nodename, CString attrname)
	{

		MSXML2::IXMLDOMNodeListPtr pChildNodes = pDOMDoc->GetchildNodes();
		if (pChildNodes == NULL)//seems this never gonna happen, no harm though
			return "";

		USES_CONVERSION;

		MSXML2::IXMLDOMNodePtr pChild;
		try
		{
			while(pChild = pChildNodes->nextNode())
			{
				CString str = queryAttrValueFromNode(pChild, nodename, attrname);
				if(!str.IsEmpty())
					return str;
			
			}
		}
		catch(_com_error e)
		{
			throw "Fail to decode XML file.";
		}
		catch(...)
		{
			throw "Fail to decode XML file.";
		}	

		return "";
	}

	/**
	* get the value of attribute attrname2 from node nodename where attribute attrname1=attrvalue1
	*
	* !It's targeted at Softscan .vol file, it's actually an xml file
	*/
	vector< pair<CString, CString> > getAttrsValues(MSXML2::IXMLDOMDocumentPtr pDOMDoc, CString nodename)
	{
		vector< pair<CString, CString> > vec;

		MSXML2::IXMLDOMNodeListPtr pChildNodes = pDOMDoc->GetchildNodes();
		if (pChildNodes == NULL)//seems this never gonna happen, no harm though
			return vec;

		USES_CONVERSION;

		MSXML2::IXMLDOMNodePtr pChild;
		try
		{
			while(pChild = pChildNodes->nextNode())
			{
				vector< pair<CString, CString> > v = queryAttrsByNodeName(pChild, nodename);
				if(!v.empty())
					return v;
			}
		}
		catch(_com_error e)
		{
			throw "Fail to decode XML file.";
		}
		catch(...)
		{
			throw "Fail to decode XML file.";
		}	

		return vec;
	}

	void getImageSize(vector< pair<CString, CString> > sizepair, int& width, int& height, int& layer)
	{
		for(int i=0; i<sizepair.size(); i++)
		{
			pair<CString, CString> p = sizepair[i];
			CString coord = p.first;
			if(coord=="X")
				width = atoi(p.second);
			if(coord=="Y")
				height = atoi(p.second);
			if(coord=="Z")
				layer = atoi(p.second);
		}
	}

	vector<float> String2FloatImage(string slice)
	{
		//char arr[4]={0xf8,0x56, 0x74,0x41};//417456f8
		//char arr[4]={0x00,0x00, 0x80,0x3f};//3f800000
		//float *b=(float *)arr;
		//printf("%e %g\n",*b,*b);

		std::vector<float> v;
		
		for(int i=0; i<=slice.size()-8; i=i+8)
		{
			std::string volfloat=slice.substr(i, 8);
			std::string s1="0x" + volfloat.substr(6, 2);
			std::string s2="0x" + volfloat.substr(4, 2);
			std::string s3="0x" + volfloat.substr(2, 2);
			std::string s4="0x" + volfloat.substr(0, 2);
			
			char a1=0, a2=0, a3=0, a4=0;

			sscanf(s1.c_str(),"%x", &a1);
			sscanf(s2.c_str(),"%x", &a2);
			sscanf(s3.c_str(),"%x", &a3);
			sscanf(s4.c_str(),"%x", &a4);

			char arr[4] = {a1, a2, a3, a4};
			float *b=(float *)arr;

			v.push_back(*b);
		}
		return v;
	}

	vector<unsigned char> String2CharImage(string slice)
	{
		//char arr[4]={0xf8,0x56, 0x74,0x41};//417456f8
		//char arr[4]={0x00,0x00, 0x80,0x3f};//3f800000
		//float *b=(float *)arr;
		//printf("%e %g\n",*b,*b);

		std::vector<unsigned char> v;
		
		for(int i=0; i<=slice.size()-8; i=i+8)
		{
			std::string volfloat=slice.substr(i, 8);
			std::string s1="0x" + volfloat.substr(6, 2);
			std::string s2="0x" + volfloat.substr(4, 2);
			std::string s3="0x" + volfloat.substr(2, 2);
			std::string s4="0x" + volfloat.substr(0, 2);
			
			char a1=0, a2=0, a3=0, a4=0;

			sscanf(s1.c_str(),"%x", &a1);
			sscanf(s2.c_str(),"%x", &a2);
			sscanf(s3.c_str(),"%x", &a3);
			sscanf(s4.c_str(),"%x", &a4);

			char arr[4] = {a1, a2, a3, a4};
			float *b=(float *)arr;

			unsigned char intt=(unsigned char)*b;
			v.push_back(intt);
		}
		return v;
	}
}
#endif //DECODING_XML_FILE_HPP_INCLUDE_GUARD_GJH676SFG8976BY