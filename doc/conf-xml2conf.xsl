<xsl:stylesheet version="1.0" 
		xmlns:xsl="http://www.w3.org/1999/XSL/Transform">

<xsl:template match="/">
  <xsl:for-each select="config/section[@target = 'config']">
    <xsl:if test="@config = '%{name}'">
      <xsl:value-of select="description" disable-output-escaping="yes"/>
    </xsl:if>
  </xsl:for-each>
  <xsl:for-each select="config/confsection[@config = '%{name}']">
    <xsl:for-each select="optiongroup">
<xsl:if test="string-length(title) > 0">


### <xsl:value-of select="title"/>
<xsl:if test="description">
=xxxstart <xsl:value-of select="description"/> =xxxend </xsl:if>
</xsl:if>
      <xsl:for-each select="option">
<xsl:if test="string-length(description) > 0">

=xxxstart <xsl:value-of select="description"/> =xxxend <xsl:if test="string-length(@default) > 0">
#<xsl:value-of select="@name"/> = <xsl:value-of select="@default"/>
</xsl:if><xsl:if test="string-length(@default) = 0"> <!-- else -->
#<xsl:value-of select="@name"/> = <xsl:value-of select="@value"/>
</xsl:if>
</xsl:if>
</xsl:for-each>


<xsl:value-of select="footer[@target = 'config']"/>
</xsl:for-each>
</xsl:for-each>
 </xsl:template>
</xsl:stylesheet>
