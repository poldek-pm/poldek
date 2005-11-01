<xsl:stylesheet version="1.0" 
		xmlns:xsl="http://www.w3.org/1999/XSL/Transform">

<!-- poldek.conf.xml -> conf_sections.c -->
<!-- $Id$ -->

<xsl:output method="text" indent="no" />
<xsl:template match="/">
/* This file is generated from poldek.conf.xml. Do not edit */
#include &lt;stdlib.h&gt;
#include "conf_intern.h"
#include "poldek_ts.h"
  <xsl:for-each select="config/confsection">
static struct poldek_conf_tag <xsl:value-of select="@name"/>_tags[] = {
      <xsl:for-each select="optiongroup">
        <xsl:for-each select="option">
   { "<xsl:value-of select="@name"/>", 
     CONF_TYPE_<xsl:value-of select="translate(@type,'abcdefghijklmnopqrstuvwxyz','ABCDEFGHIJKLMNOPQRSTUVWXYZ')"/> 
             <xsl:if test="@multiple='yes'"> | CONF_TYPE_F_MULTI</xsl:if>
             <xsl:if test="@required='yes'"> | CONF_TYPE_F_REQUIRED</xsl:if>
             <xsl:if test="@env='yes'"> | CONF_TYPE_F_ENV</xsl:if>
             <xsl:if test="@list='yes'"> | CONF_TYPE_F_LIST</xsl:if>
             <xsl:if test="@path='yes'"> | CONF_TYPE_F_PATH</xsl:if>
             <xsl:if test="@obsoleted='yes'"> | CONF_TYPE_F_OBSL</xsl:if>,
             <xsl:choose>
               <xsl:when test="string-length(@default) > 0">"<xsl:value-of select="@default"/>"</xsl:when>
               <xsl:otherwise>NULL</xsl:otherwise>
             </xsl:choose>,
             <xsl:choose>
               <xsl:when test="string-length(@op) > 0">POLDEK_OP_<xsl:value-of select="@op"/></xsl:when>
               <xsl:otherwise>0</xsl:otherwise>
             </xsl:choose>, { 0 } },
             <xsl:for-each select="alias">
          {  "<xsl:value-of select="@name"/>", CONF_TYPE_F_ALIAS<xsl:if test="@obsoleted='yes'"> | CONF_TYPE_F_OBSL</xsl:if>, NULL, 0, { 0 } },
             </xsl:for-each>
             
        </xsl:for-each>
      </xsl:for-each>
          { NULL, 0, NULL, 0, { 0 } }
};

    </xsl:for-each>


struct poldek_conf_section poldek_conf_sections[] = {
  <xsl:for-each select="config/confsection">
    { "<xsl:value-of select="@name"/>", <xsl:value-of select="@name"/>_tags, 
       <xsl:choose>
         <xsl:when test="@multiple='yes'">1</xsl:when>
         <xsl:otherwise>0</xsl:otherwise>
       </xsl:choose>,
    },
 </xsl:for-each>   
    { NULL, 0, 0 }
};

</xsl:template>
</xsl:stylesheet>

