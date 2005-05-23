<xsl:stylesheet version="1.0" 
		xmlns:xsl="http://www.w3.org/1999/XSL/Transform">

<xsl:output indent="yes" />
<xsl:template match="/">
&lt;!DOCTYPE refentry PUBLIC "-//OASIS//DTD DocBook XML V4.1.2//EN"
"http://www.oasis-open.org/docbook/xml/4.1.2/docbookx.dtd"&gt;
<refentry id="{config/@name}">
<refmeta>
  <refentrytitle><xsl:value-of select="config/@name"/></refentrytitle>
  <manvolnum>1</manvolnum>
</refmeta>

<refnamediv>
<refname><xsl:value-of select="config/@name"/></refname>
<refpurpose>the configuration file for poldek</refpurpose>
</refnamediv>

<xsl:for-each select="config/section[@target = 'manual']">
<refsect1>
  <title><xsl:value-of select="title"/></title>
  <xsl:value-of select="description" disable-output-escaping="yes"/>
</refsect1>
</xsl:for-each>

  <xsl:for-each select="config/confsection">
    <refsect1><title>Parameters of [<xsl:value-of select="@name"/>] section</title>
    <xsl:if test="description">
      <para><xsl:value-of select="description"/></para>
    </xsl:if>
      <xsl:for-each select="optiongroup">
        <refsect2><title><xsl:value-of select="title"/></title> 
        <synopsis>
          <xsl:value-of select="description"/>
        </synopsis>
        <variablelist><title></title>
        <xsl:for-each select="option">
          <xsl:if test="string-length(description) > 0">
            <varlistentry><term><option><xsl:value-of select="@name"/></option></term>
            <listitem>
              <para>
                <xsl:value-of select="description" disable-output-escaping="yes" />
              </para>
              <xsl:if test="@default">
                <para>
                  Default: <xsl:value-of select="@name"/> = <xsl:value-of select="@default"/>
                </para>
              </xsl:if>
            </listitem>
          </varlistentry>
          </xsl:if>
        </xsl:for-each>
      </variablelist>
    </refsect2>
  </xsl:for-each>
</refsect1>
</xsl:for-each>
</refentry>
</xsl:template>
</xsl:stylesheet>

