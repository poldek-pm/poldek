<!-- convert NEWS.xml to docbook document -->
<xsl:stylesheet version="1.0" 
                xmlns:xsl="http://www.w3.org/1999/XSL/Transform">

<xsl:output method="xml"
    doctype-public="-//OASIS//DTD DocBook XML V4.1.2//EN"
    doctype-system="http://www.oasis-open.org/docbook/xml/4.2/docbookx.dtd"
    encoding="UTF-8"
    indent="yes"/>
<xsl:strip-space elements="*"/>

<xsl:template match="/">
<article lang="en">
<articleinfo>
<title>poldek NEWS</title>
<releaseinfo><xsl:value-of select="news/cvsid"/></releaseinfo>
</articleinfo>

<xsl:apply-templates select="news"/>
</article>
</xsl:template>

<xsl:template match="news">
<xsl:for-each select="release[string-length(@date) > 0]">
  <xsl:text>  
  </xsl:text>

  <xsl:variable name="id_version">
      <xsl:value-of select="translate(@version, '.', '_')"/>
  </xsl:variable>
  <sect1 id="v{$id_version}"><title><xsl:value-of select="@version"/> (<xsl:value-of select="@focus"/>; <xsl:value-of select="@date"/>)</title>
  <xsl:if test="para">
    <para><xsl:value-of select="para"/></para>
  </xsl:if>


  <xsl:choose>
    <xsl:when test='section'>
      <xsl:for-each select="section">
        <sect2><title><xsl:value-of select="title"/></title>
        <itemizedlist>
          <xsl:apply-templates select="entry"/>
        </itemizedlist>
        </sect2>
      </xsl:for-each>
    </xsl:when>
    <xsl:otherwise>
      <itemizedlist>
        <xsl:apply-templates select="entry"/>
      </itemizedlist>
    </xsl:otherwise>
  </xsl:choose>
</sect1>
</xsl:for-each>
</xsl:template>

<xsl:template match="entry">
  <xsl:text>
  </xsl:text>
  <listitem>
  <xsl:if test="count(para) = 1"> 
    <para> 
    <xsl:value-of select="para"/> 
    <xsl:if test="author">
      (<xsl:apply-templates select="author"/>)
    </xsl:if>
    </para>
  </xsl:if>
  <xsl:if test="count(para) > 1"> 
    <xsl:for-each select="para">
      <xsl:choose>
        <xsl:when test='@nowrap'>
          <screen>
            <xsl:value-of select="."/> 
          </screen>
        </xsl:when>
        <xsl:otherwise>
          <para>
            <xsl:value-of select="."/> 
          </para>
        </xsl:otherwise>
      </xsl:choose>
    </xsl:for-each>
  </xsl:if>
  </listitem>
</xsl:template>


<xsl:template match="author">
  <emphasis><xsl:if test="@part">with help of </xsl:if><xsl:value-of select="."/></emphasis><xsl:if test="@email"> &lt;<emphasis><xsl:value-of select="@email"/></emphasis>&gt;</xsl:if><xsl:if test="not(position()=last())">, </xsl:if>
</xsl:template>

<xsl:template match="para">
</xsl:template>


</xsl:stylesheet>


