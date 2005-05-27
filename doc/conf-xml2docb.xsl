<xsl:stylesheet version="1.0" 
		xmlns:xsl="http://www.w3.org/1999/XSL/Transform">

<xsl:output indent="yes" />
<xsl:template match="/">

<xsl:for-each select="config/section[@target = 'manual']">
  <xsl:if test="@id != 'synopsis'">
  <sect2 id="configuration.{@id}">
   <title><xsl:value-of select="title"/></title>
   <xsl:value-of select="description" disable-output-escaping="yes"/>
  </sect2>
  </xsl:if>
</xsl:for-each>

  <xsl:for-each select="config/confsection">
    <sect2 id="configuration.{@name}"><title>Parameters of [<xsl:value-of select="@name"/>] section</title>
    <xsl:if test="description">
      <para><xsl:value-of select="description"/></para>
    </xsl:if>
      <xsl:for-each select="optiongroup">
        <sect3><title><xsl:value-of select="title"/></title> 
        <para>
          <xsl:value-of select="description"/>
        </para>
        <variablelist><title></title>
        <xsl:for-each select="option">
          <xsl:if test="string-length(description) > 0">
            <xsl:variable name="id_option">
              <xsl:value-of select="translate(@name, ' ', '_')"/>
            </xsl:variable>
            <varlistentry><term><option id="configuration.{../../@name}.{$id_option}"><xsl:value-of select="@name"/></option></term>
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
    </sect3>
  </xsl:for-each>
</sect2>
</xsl:for-each>
</xsl:template>
</xsl:stylesheet>

